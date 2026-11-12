// IINA JustXMLRPC.swift and JavascriptAPIHttp.swift value conversion.
// Copyright (c) 2017 lhc and IINA contributors. GPL-3.0; references/iina/LICENSE.
#pragma once
#include <string>

namespace iima {
// Worker-only, no network or filesystem access. COM/WinRT is initialized on the
// calling thread for each operation. This is the JSON-representable XML-RPC
// subset, not a generic XML/JavaScript serializer.
class PluginXmlRpc final {
public:
    // argsJson must be a JSON array <=64 KiB. Supported recursively: boolean,
    // int32-compatible numbers, finite double, UTF-8 string, array and object.
    // Integral JSON numbers fitting int32 use <int>, others use <double>.
    // Rejects null, invalid XML characters, deep/large objects and unsupported
    // method names; no Date/base64 coercions. Returned UTF-8 XML <=1 MiB.
    static std::string Encode(std::string const& method, std::string const& argsJson);
    // UTF-8 response <=1 MiB. Exactly one success value or one validated fault
    // struct. Returns JSON {fault:boolean,value:<decoded value>} <=8 MiB.
    // Value nesting <=16, XML element nesting <=64 and DOM nodes <=4096.
    // Rejects DTD/external entities, namespaces, attributes, duplicate struct
    // keys, unsupported types, non-finite numbers and malformed envelopes.
    static std::string Decode(std::string const& responseXml);
};
}
