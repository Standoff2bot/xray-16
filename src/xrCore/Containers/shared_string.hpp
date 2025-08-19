#pragma once

namespace xray
{
class shared_string_header;

class shared_string final
{
    // to make it short
    using self_t            =   shared_string;
    using header_t          =   shared_string_header;

public:
                                shared_string   () = default;

                                shared_string   (pcstr string);
                                shared_string   (pcstr string, size_t size);

                                shared_string   (const shared_string& other);
                                shared_string   (shared_string&&) = default;

                                ~shared_string  ();

                    self_t&     operator=       (pcstr string);

                    self_t&     operator=       (const shared_string& other);
                    self_t&     operator=       (shared_string&&) = default;

    // Iterators
    [[nodiscard]]   pcstr       begin           () const { return data(); }
    [[nodiscard]]   pcstr       cbegin          () const { return data(); }

    [[nodiscard]]   pcstr       end             () const;
    [[nodiscard]]   pcstr       cend            () const;

    [[nodiscard]]   pcstr       rbegin          () const;
    [[nodiscard]]   pcstr       crbegin         () const;

    [[nodiscard]]   pcstr       rend            () const { return data() - 1; }
    [[nodiscard]]   pcstr       crend           () const { return data() - 1; }

    // Element access
    [[nodiscard]]   pcstr       data            () const;
    [[nodiscard]]   pcstr       c_str           () const { return data(); }

    [[nodiscard]]   char        front           () const { return data()[0]; }
    [[nodiscard]]   char        back            () const;
    [[nodiscard]]   char        operator[]      (size_t idx) const { return data()[idx]; }

    [[nodiscard]]   u32         id              () const { return m_offset; }

    // Capacity
    [[nodiscard]]   size_t      size            () const;
    [[nodiscard]]   size_t      length          () const { return size(); }
    [[nodiscard]]   bool        empty           () const { return size() == 0; }

    // Modifiers
                    void        swap            (shared_string& other) noexcept
                    {
                        const auto tmp = m_offset;
                        m_offset = other.m_offset;
                        other.m_offset = tmp;
                    }

                    self_t&     printf          (pcstr format, ...)
                    {
                        string4096 buf;
                        va_list p;
                        va_start(p, format);
                        int vs_sz = vsnprintf(buf, sizeof(buf) - 1, format, p);
                        buf[sizeof(buf) - 1] = 0;
                        va_end(p);
                        if (vs_sz)
                            operator=(buf);
                        return *this;
                    }

private:
    [[nodiscard]]   header_t*   header          () const;

private:
    u32 m_offset{};
};

[[nodiscard]] inline auto operator""_shrd(pcstr string, size_t size) noexcept
{
    return shared_string{ string, size };
}

[[nodiscard]] ICF bool operator==(const shared_string& a, const shared_string& b)
{
    return a.id() == b.id();
}

ICF void swap(shared_str& lhs, shared_str& rhs) noexcept { lhs.swap(rhs); }
} // namespace xray
