#pragma once

namespace xray
{
class shared_string_header
{
public:

};

class shared_string_pool
{
public:
    size_t collect_garbage();
};

XRCORE_API inline shared_string_pool s_string_pool;
} // namespace xray
