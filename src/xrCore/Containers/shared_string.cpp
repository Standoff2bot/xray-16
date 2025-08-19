#include "stdafx.h"

#include "shared_string.hpp"
#include "shared_string_pool.hpp"

namespace xray
{
shared_string::shared_string(pcstr string)
{
    
}

shared_string::shared_string(pcstr string, size_t size)
{
    
}

shared_string::shared_string(const shared_string& other)
{
    
}

shared_string::~shared_string()
{
    
}

shared_string& shared_string::operator=(pcstr string)
{
    return *this;
}

shared_string& shared_string::operator=(const shared_string& other)
{
    return *this;
}

pcstr shared_string::end() const
{
    return nullptr;
}

pcstr shared_string::cend() const
{
    return nullptr;
}

pcstr shared_string::rbegin() const
{
    return nullptr;
}

pcstr shared_string::crbegin() const
{
    return nullptr;
}

pcstr shared_string::data() const
{
    return nullptr;
}

char shared_string::back() const
{
    return 0;
}

size_t shared_string::size() const
{
    return 0;
}

shared_string_header* shared_string::header() const
{
    return nullptr;
}
} // namespace xray
