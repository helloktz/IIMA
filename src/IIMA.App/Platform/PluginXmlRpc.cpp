// Static port of IINA JustXMLRPC.toValueElement/value(fromValueElement:)/call
// and JavascriptAPIXmlrpc.call. Cocoa XML trees replaced by Windows XML DOM.
// Copyright (c) 2017 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#include "pch.h"
#include "PluginXmlRpc.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace iima {
namespace {
using namespace winrt;
using namespace Windows::Data::Json;
using namespace Windows::Data::Xml::Dom;
constexpr std::size_t MaxArguments = 64 * 1024;
constexpr std::size_t MaxXml = 1024 * 1024;
constexpr std::size_t MaxResult = 8 * MaxXml;
constexpr unsigned MaxDepth = 16;
constexpr unsigned MaxNodes = 4096;
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() { if (FAILED(status) && status != RPC_E_CHANGED_MODE) check_hresult(status); }
    ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};
[[noreturn]] void Invalid() { throw std::invalid_argument("Unsupported or malformed XML-RPC value."); }
std::wstring Wide(std::string_view input) {
    if (input.empty()) return {};
    if (input.size() > MaxResult) Invalid();
    auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (!count) Invalid();
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    check_bool(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), result.data(), count));
    return result;
}
void XmlText(std::wstring_view text) {
    // XML 1.0 scalar values. JSON escaped lone surrogates must not be repaired
    // silently by a later UTF-8 conversion or DOM serializer.
    for (std::size_t index = 0; index < text.size(); ++index) {
        auto c = static_cast<unsigned>(text[index]);
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (++index >= text.size()) Invalid();
            auto low = static_cast<unsigned>(text[index]);
            if (low < 0xDC00 || low > 0xDFFF) Invalid();
        } else if ((c < 0x20 && c != 9 && c != 10 && c != 13) ||
            (c >= 0xDC00 && c <= 0xDFFF) || c == 0xFFFE || c == 0xFFFF) Invalid();
    }
}
void ArgumentBudget(std::string_view text) {
    // Bound parser nesting before creating a WinRT JSON tree. String escapes
    // are accounted for; syntax validation remains with JsonArray::Parse.
    unsigned depth{}, tokens{}; bool quoted{}, escaped{};
    for (char c : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') { quoted = true; if (++tokens > MaxNodes) Invalid(); }
        else if (c == '{' || c == '[') { if (++depth > MaxDepth + 1 || ++tokens > MaxNodes) Invalid(); }
        else if (c == '}' || c == ']') { if (!depth) Invalid(); --depth; }
        else if (c == ',' && ++tokens > MaxNodes) Invalid();
    }
    if (quoted || depth) Invalid();
}
struct Budget {
    unsigned nodes{};
    void Take(unsigned depth) { if (depth > MaxDepth || ++nodes > MaxNodes) Invalid(); }
};
XmlElement Element(XmlDocument const& doc, wchar_t const* name) { return doc.CreateElement(name); }
XmlElement TextElement(XmlDocument const& doc, wchar_t const* name, hstring const& text) {
    XmlText(std::wstring_view(text));
    auto element = Element(doc, name);
    element.AppendChild(doc.CreateTextNode(text));
    return element;
}
std::string Number(double number) {
    if (!std::isfinite(number)) Invalid();
    char buffer[64];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (error != std::errc{}) Invalid();
    return {buffer, end};
}
XmlElement EncodeValue(XmlDocument const& doc, IJsonValue const& input, Budget& budget, unsigned depth) {
    budget.Take(depth);
    auto value = Element(doc, L"value");
    switch (input.ValueType()) {
    case JsonValueType::Boolean:
        value.AppendChild(TextElement(doc, L"boolean", input.GetBoolean() ? L"1" : L"0")); break;
    case JsonValueType::Number: {
        auto number = input.GetNumber();
        bool integral = std::isfinite(number) && std::trunc(number) == number &&
            number >= std::numeric_limits<std::int32_t>::min() && number <= std::numeric_limits<std::int32_t>::max();
        value.AppendChild(TextElement(doc, integral ? L"int" : L"double", to_hstring(Number(number)))); break;
    }
    case JsonValueType::String:
        value.AppendChild(TextElement(doc, L"string", input.GetString())); break;
    case JsonValueType::Array: {
        auto array = Element(doc, L"array"), data = Element(doc, L"data");
        for (auto const& item : input.GetArray()) data.AppendChild(EncodeValue(doc, item, budget, depth + 1));
        array.AppendChild(data); value.AppendChild(array); break;
    }
    case JsonValueType::Object: {
        auto structure = Element(doc, L"struct");
        for (auto const& item : input.GetObject()) {
            budget.Take(depth); // also bound member names, not just values
            auto member = Element(doc, L"member");
            member.AppendChild(TextElement(doc, L"name", item.Key()));
            member.AppendChild(EncodeValue(doc, item.Value(), budget, depth + 1));
            structure.AppendChild(member);
        }
        value.AppendChild(structure); break;
    }
    default: Invalid(); // null, Date and Data have no unambiguous JSON encoding
    }
    return value;
}
bool Space(std::wstring_view text) {
    return std::all_of(text.begin(), text.end(), [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; });
}
void InspectTree(IXmlNode const& node, unsigned& nodes, unsigned depth) {
    if (++nodes > MaxNodes || depth > 64) Invalid();
    auto type = node.NodeType();
    if (type == NodeType::ElementNode) {
        if (node.Attributes().Length() != 0 || std::wstring_view(node.NodeName()).find(L':') != std::wstring_view::npos) Invalid();
    } else if (type != NodeType::TextNode && type != NodeType::DataSectionNode && type != NodeType::CommentNode) Invalid();
    auto children = node.ChildNodes();
    for (std::uint32_t i = 0; i < children.Length(); ++i) InspectTree(children.Item(i), nodes, depth + 1);
}
std::vector<IXmlNode> Elements(IXmlNode const& node) {
    std::vector<IXmlNode> result;
    for (auto const& child : node.ChildNodes()) {
        if (child.NodeType() == NodeType::ElementNode) result.push_back(child);
        else if (child.NodeType() == NodeType::CommentNode) continue;
        else if ((child.NodeType() != NodeType::TextNode && child.NodeType() != NodeType::DataSectionNode) ||
            !Space(std::wstring_view(child.InnerText()))) Invalid();
    }
    return result;
}
void Named(IXmlNode const& node, wchar_t const* name) { if (node.NodeName() != name) Invalid(); }
hstring Text(IXmlNode const& node) {
    for (auto const& child : node.ChildNodes())
        if (child.NodeType() != NodeType::TextNode && child.NodeType() != NodeType::DataSectionNode &&
            child.NodeType() != NodeType::CommentNode) Invalid();
    return node.InnerText();
}
IJsonValue DecodeValue(IXmlNode const& node, Budget& budget, unsigned depth) {
    budget.Take(depth); Named(node, L"value");
    // Untyped <value> is a string, including the empty element.
    bool typed{};
    for (auto const& child : node.ChildNodes()) if (child.NodeType() == NodeType::ElementNode) { typed = true; break; }
    if (!typed) return JsonValue::CreateStringValue(Text(node));
    auto children = Elements(node); if (children.size() != 1) Invalid();
    auto item = children.front(); auto name = item.NodeName();
    if (name == L"string") return JsonValue::CreateStringValue(Text(item));
    if (name == L"boolean") {
        auto text = Text(item); if (text != L"0" && text != L"1") Invalid();
        return JsonValue::CreateBooleanValue(text == L"1");
    }
    if (name == L"int" || name == L"i4") {
        auto text = to_string(Text(item));
        std::string_view digits(text); if (digits.starts_with('+')) digits.remove_prefix(1);
        if (digits.empty() || (text.front() == '+' && digits.front() == '-')) Invalid();
        std::int32_t value{};
        auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) Invalid();
        return JsonValue::CreateNumberValue(value);
    }
    if (name == L"double") {
        auto text = to_string(Text(item));
        std::string_view digits(text); if (digits.starts_with('+')) digits.remove_prefix(1);
        if (digits.empty() || (text.front() == '+' && digits.front() == '-')) Invalid();
        double value{};
        auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, std::chars_format::general);
        if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size() || !std::isfinite(value)) Invalid();
        return JsonValue::CreateNumberValue(value);
    }
    if (name == L"array") {
        auto children = Elements(item); if (children.size() != 1) Invalid(); Named(children.front(), L"data");
        JsonArray array;
        for (auto const& value : Elements(children.front())) array.Append(DecodeValue(value, budget, depth + 1));
        return array;
    }
    if (name == L"struct") {
        JsonObject object;
        for (auto const& member : Elements(item)) {
            budget.Take(depth); Named(member, L"member");
            auto children = Elements(member); if (children.size() != 2) Invalid();
            Named(children[0], L"name"); Named(children[1], L"value");
            auto key = Text(children[0]); if (object.HasKey(key)) Invalid();
            object.Insert(key, DecodeValue(children[1], budget, depth + 1));
        }
        return object;
    }
    Invalid(); // dateTime.iso8601, base64, nil and extension types are unsupported
}
}
std::string PluginXmlRpc::Encode(std::string const& method, std::string const& argsJson) {
    if (method.empty() || method.size() > 256 || argsJson.empty() || argsJson.size() > MaxArguments ||
        !std::all_of(method.begin(), method.end(), [](unsigned char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '.' || c == ':' || c == '/';
        })) Invalid();
    // At most 6 output bytes per input byte for XML character escaping, plus
    // 128 bytes of markup per bounded semantic node/member and envelope. This
    // preflight bounds DOM serialization allocation even before the final size
    // check. Surrogate escape sequences in JSON can only shrink this estimate.
    if (argsJson.size() * 6 + MaxNodes * 128 + method.size() * 6 + 256 > MaxXml) Invalid();
    ArgumentBudget(argsJson); Apartment apartment;
    auto args = JsonArray::Parse(Wide(argsJson)); Budget budget;
    XmlDocument document;
    auto root = Element(document, L"methodCall");
    root.AppendChild(TextElement(document, L"methodName", to_hstring(method)));
    auto params = Element(document, L"params");
    for (auto const& argument : args) {
        auto param = Element(document, L"param");
        param.AppendChild(EncodeValue(document, argument, budget, 1)); params.AppendChild(param);
    }
    root.AppendChild(params); document.AppendChild(root);
    unsigned nodes{}; InspectTree(root, nodes, 1);
    auto result = to_string(document.GetXml()); if (result.size() > MaxXml) Invalid(); return result;
}
std::string PluginXmlRpc::Decode(std::string const& responseXml) {
    if (responseXml.empty() || responseXml.size() > MaxXml) Invalid();
    Apartment apartment; XmlLoadSettings settings;
    settings.ProhibitDtd(true); settings.ResolveExternals(false);
    settings.MaxElementDepth(64); settings.ValidateOnParse(false); settings.ElementContentWhiteSpace(true);
    XmlDocument document; document.LoadXml(Wide(responseXml), settings);
    auto root = document.DocumentElement(); if (!root) Invalid(); Named(root, L"methodResponse");
    unsigned nodes{}; InspectTree(root, nodes, 1);
    // The XML parser enforces a single document element. Reject processing
    // instructions outside it too, except the ordinary XML declaration.
    for (auto const& child : document.ChildNodes()) {
        if (child.NodeType() == NodeType::ElementNode) continue;
        if (++nodes > MaxNodes) Invalid();
        if (child.NodeType() == NodeType::CommentNode) continue;
        if (child.NodeType() == NodeType::TextNode && Space(std::wstring_view(child.InnerText()))) continue;
        if (child.NodeType() == NodeType::ProcessingInstructionNode && child.NodeName() == L"xml") continue;
        Invalid();
    }
    auto branches = Elements(root); if (branches.size() != 1) Invalid();
    bool fault = branches[0].NodeName() == L"fault";
    IXmlNode value{nullptr};
    auto contents = Elements(branches[0]);
    if (fault) {
        if (contents.size() != 1) Invalid(); value = contents.front();
    } else {
        Named(branches[0], L"params"); if (contents.size() != 1) Invalid(); Named(contents[0], L"param");
        auto values = Elements(contents[0]); if (values.size() != 1) Invalid(); value = values.front();
    }
    Budget budget; auto decoded = DecodeValue(value, budget, 1);
    if (fault) {
        if (decoded.ValueType() != JsonValueType::Object) Invalid();
        auto object = decoded.GetObject();
        if (object.Size() != 2 || !object.HasKey(L"faultCode") || !object.HasKey(L"faultString") ||
            object.GetNamedValue(L"faultCode").ValueType() != JsonValueType::Number ||
            object.GetNamedValue(L"faultString").ValueType() != JsonValueType::String) Invalid();
        auto code = object.GetNamedNumber(L"faultCode");
        if (std::trunc(code) != code || code < std::numeric_limits<std::int32_t>::min() ||
            code > std::numeric_limits<std::int32_t>::max()) Invalid();
    }
    JsonObject envelope; envelope.Insert(L"fault", JsonValue::CreateBooleanValue(fault)); envelope.Insert(L"value", decoded);
    auto result = to_string(envelope.Stringify()); if (result.size() > MaxResult) Invalid(); return result;
}
}
