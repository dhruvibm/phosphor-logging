/**
 * Copyright © 2026 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "extensions/openpower-pels/paths.hpp"
#include "extensions/openpower-pels/user_data_json.hpp"

#include <Python.h>

#include <nlohmann/json.hpp>

#include <fstream>

#include <gtest/gtest.h>

using namespace openpower::pels;
using JSON = nlohmann::ordered_json;

class UserDataJSONTest : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        std::ofstream componentFile(getPELReadOnlyDataPath() /
                                    "O_component_ids.json");
        componentFile << R"({"2000":"BMC: error logging"})";
        Py_Initialize();
    }

    static void TearDownTestSuite()
    {
        Py_Finalize();
    }

    void SetUp() override
    {
        ASSERT_EQ(PyRun_SimpleString(R"(
import json
import sys
import types
for name in ('udparsers', 'udparsers.o3000'):
    package = types.ModuleType(name)
    package.__path__ = []
    sys.modules[name] = package
entitypath = types.ModuleType('udparsers.o3000.entitypath')
def format_paths(payload):
    data = json.loads(payload)
    data[0]['EntityPath'] = '/sys-0/node-0/backplane-0/proc_module-0/hub_chip-0/mc-0'
    return json.dumps(data)
entitypath.formatJSON = format_paths
sys.modules[entitypath.__name__] = entitypath
)"),
                  0);
    }

    void TearDown() override
    {
        EXPECT_EQ(PyRun_SimpleString(R"(
for name in ('udparsers.o3000.entitypath', 'udparsers.o3000', 'udparsers'):
    sys.modules.pop(name, None)
)"),
                  0);
        EXPECT_FALSE(PyErr_Occurred());
    }

    static JSON callout()
    {
        return JSON::array(
            {{{"EntityPath", {38, 1, 0, 2, 0, 90, 0, 87, 0, 89, 0, 68, 0}},
              {"Deconfigured", false},
              {"GuardType", "FATAL"},
              {"Guarded", true},
              {"LocationCode", "Ufcs-N00-BD00-PM00"},
              {"Priority", "M"},
              {"ReconfigTargetClass", "Other"}}});
    }

    static std::vector<uint8_t> bytes()
    {
        auto text = callout().dump();
        return {text.begin(), text.end()};
    }

    static JSON display(const std::vector<uint8_t>& data,
                        const std::vector<std::string>& plugins = {"o3000"},
                        uint8_t subtype = 1)
    {
        auto result = user_data::getJSON(0x2000, subtype, 1, data, 'O', plugins);
        EXPECT_TRUE(result.has_value());
        return result ? JSON::parse("{" + *result + "}") : JSON{};
    }
};

TEST_F(UserDataJSONTest, FormatsDisplayWithoutChangingCalloutData)
{
    auto data = bytes();
    const auto original = data;
    auto result = display(data);
    EXPECT_EQ(result["Data"][0]["EntityPath"],
              "/sys-0/node-0/backplane-0/proc_module-0/hub_chip-0/mc-0");
    EXPECT_EQ(result["Created by"], "BMC: error logging");
    auto expected = callout();
    expected[0].erase("EntityPath");
    result["Data"][0].erase("EntityPath");
    EXPECT_EQ(result["Data"], expected);
    EXPECT_EQ(data, original);
}

TEST_F(UserDataJSONTest, FormatsCBORDisplayWithoutChangingPayload)
{
    auto data = JSON::to_cbor(callout());
    // Builtin CBOR ends in a network-order pad count, including zero padding.
    uint32_t pad = (4 - (data.size() % 4)) % 4;
    data.resize(data.size() + pad, 0);
    data.insert(data.end(), {0, 0, 0, static_cast<uint8_t>(pad)});
    const auto original = data;
    auto result = display(data, {"o3000"}, 2);
    EXPECT_EQ(result["Data"][0]["EntityPath"],
              "/sys-0/node-0/backplane-0/proc_module-0/hub_chip-0/mc-0");
    EXPECT_EQ(data, original);
}

TEST_F(UserDataJSONTest, NoPluginLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "entitypath.formatJSON = lambda payload: 1 / 0"),
              0);
    EXPECT_EQ(display(bytes(), {})["Data"], callout());
}

TEST_F(UserDataJSONTest, MissingModuleLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "sys.modules.pop('udparsers.o3000.entitypath')"),
              0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST_F(UserDataJSONTest, MissingFunctionLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString("del entitypath.formatJSON"), 0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST_F(UserDataJSONTest, FormatterExceptionLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "entitypath.formatJSON = lambda payload: 1 / 0"),
              0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST_F(UserDataJSONTest, BadJSONLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "entitypath.formatJSON = lambda payload: '{invalid'"),
              0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST_F(UserDataJSONTest, ChangedJSONTypeLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "entitypath.formatJSON = lambda payload: '{}'"),
              0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST_F(UserDataJSONTest, NonStringResultLeavesBuiltinJSONIntact)
{
    ASSERT_EQ(PyRun_SimpleString(
                  "entitypath.formatJSON = lambda payload: None"),
              0);
    EXPECT_EQ(display(bytes())["Data"], callout());
}

TEST(UserDataJSONWithoutPythonTest, BuiltinJSONStillWorks)
{
    ASSERT_FALSE(Py_IsInitialized());
    std::string text = R"({"EntityPath":[33,1,0]})";
    std::vector<uint8_t> data{text.begin(), text.end()};
    auto result = user_data::getJSON(0x2000, 1, 1, data, 'O', {"o3000"});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(JSON::parse("{" + *result + "}")["EntityPath"],
              JSON::array({33, 1, 0}));
}
