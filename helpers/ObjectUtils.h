/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2020 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

#ifndef __OBJECTUTILS_H__
#define __OBJECTUTILS_H__
using namespace WPEFramework;
using namespace std;
class ObjectUtils {
    public:
        // Implement a static method which accepts a JsonObject and a key string
        // and returns true if the key exists in the JsonObject and its value is boolean
        static bool HasBooleanEntry(const JsonObject& obj, const string& key, bool& resultValue) {
            if (!obj.HasLabel(key.c_str())) {
                return false;
            }
            const auto& value = obj[key.c_str()];
            if (value.IsNull()) {
                return false;
            } else if(value.Content() == Core::JSON::Variant::type::BOOLEAN) {
                // add the resultValue
                resultValue = value.Boolean();
                return true;
            }
            return false;
        }

        // implement a new method which accepts a key and boolean value uses CreateBooleanObject to create a JsonObject
        // and then serializes it to a string and returns the string
        static string CreateBooleanJsonString(const string& key, const bool value) {
            JsonObject obj = CreateBooleanObject(key, value);
            string resultStr;
            obj.ToString(resultStr);
            return resultStr;
        }

        // Implement a new method which creates a JsonObject value for a given key and boolean value
        static JsonObject CreateBooleanObject(const string& key, const bool value) {
            JsonObject obj;
            ObjectUtils::AddBooleanEntry(obj, key, value);
            return obj;
        }

        // implement a new method which accepts a uint32_t value and returns a string with a serialized JsonObject with a key "value" and the uint32_t value
        static string CreateUInt32Object(const uint32_t value) {
            JsonObject obj;
            obj["value"] = value;
            string resultStr;
            obj.ToString(resultStr);
            return resultStr;
        }

        // implement a new method which accepts a string and returns a string which is a serialized JsonObject with a key "value" and the string value
        static string CreateStringObject(const string& value) {
            JsonObject obj;
            obj["value"] = value;
            string resultStr;
            obj.ToString(resultStr);
            return resultStr;
        }

        // Implement a static method which accepts a JsonObject, a key string and a boolean value
        // and adds the key-value pair to the JsonObject
        static void AddBooleanEntry(JsonObject& obj, const string& key, const bool value) {
            obj[key.c_str()] = value;
        }

        // Implement a static method which accepts a JsonObject and a key string
        // and returns true if the key exists in the JsonObject and its value is string
        static bool HasStringEntry(const JsonObject& obj, const string& key, string& resultValue) {
            if (!obj.HasLabel(key.c_str())) {
                return false;
            }
            const auto& value = obj[key.c_str()];
            if (value.IsNull()) {
                return false;
            } else if(value.Content() == Core::JSON::Variant::type::STRING) {
                // add the resultValue
                resultValue = value.String();
                return true;
            }
            return false;
        }

        // Implement a new static method which accepts a boolean and returns a 
        // serialized string "true" or "false"
        static string BoolToJsonString(const bool value) {  
            return value ? "true" : "false";
        }

        // Serializes a string as a JSON string value (quoted and escaped by
        // Thunder's JSON serializer). Use it instead of "\"" + value + "\"".
        static string StringToJsonString(const string& value) {
            Core::JSON::String json;
            json = value;
            string result;
            json.ToString(result);
            return result;
        }
};
#endif

