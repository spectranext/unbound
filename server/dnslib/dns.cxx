#include "dnslib.h"
#include "dns.hpp"

uint8_t dns_parse(uint8_t* packet, uint32_t size, char* output, uint16_t* request_id)
{
    auto out = dns::Parse(packet, size);
    if (!std::get<0>(out))
    {
        return 1;
    }

    dns::DnsMessage& msg = std::get<1>(out);
    *request_id = msg.dnsHead.xid;

    if (not msg.questions.empty())
    {
        auto& q = msg.questions[0];
        strcpy(output, q.name.c_str());
        return 0;
    }

    return 1;
}

uint8_t dns_encode(const char* name, uint16_t request_id, uint32_t ip4response, uint8_t* packet, uint32_t* outSize)
{
    dns::DnsMessage response = { .dnsHead = {
        .xid = request_id,
        .isResponse = 1
    }};

    if (ip4response == 0)
    {
        // name error
        response.dnsHead.responseCode = 3;
    }

    std::string out_name(name);

    dns::DnsQuestion q{out_name, dns::RecordType::A, dns::RecordClass::INTERNET};
    response.questions.push_back(q);

    dns::AData a = ip4response;
    dns::DnsAnswer answer {out_name, dns::RecordType::A, dns::RecordClass::INTERNET, 300, a};
    response.answers.push_back(answer);
    auto build = dns::Build(response);
    *outSize = build.size();
    memcpy(packet, build.data(), build.size());
    return 0;
}