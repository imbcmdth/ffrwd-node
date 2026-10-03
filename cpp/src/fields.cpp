#include "ffrwd/fields.hpp"

#include <algorithm>

namespace ffrwd {

Json schema_for(const Json& value) {
    Json schema = Json::object();
    switch (value.type()) {
        case Json::Type::Null:
            break;
        case Json::Type::Bool:
            schema.set("type", "boolean");
            break;
        case Json::Type::Int:
            schema.set("type", "integer");
            break;
        case Json::Type::Float:
            schema.set("type", "number");
            break;
        case Json::Type::String:
            schema.set("type", "string");
            break;
        case Json::Type::Array:
            if (value.size() > 0) schema.set("items", schema_for(value[std::size_t(0)]));
            schema.set("type", "array");
            break;
        case Json::Type::Object: {
            Json::Object fields = value.as_object();
            std::stable_sort(fields.begin(), fields.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            Json properties = Json::object();
            Json required = Json::array();
            for (const auto& [name, field] : fields) {
                properties.set(name, schema_for(field));
                if (!field.is_null()) required.push_back(name);
            }
            schema.set("properties", std::move(properties));
            schema.set("required", std::move(required));
            schema.set("type", "object");
            break;
        }
    }
    return schema;
}

}  // namespace ffrwd
