#pragma once

#include <optional>
#include <utility>
#include <variant>

#include "Error.h"

// Result<T>/Result<void> stand in for std::expected (C++23) since this project targets C++20.
// Domain/Application layers signal failure through these rather than exceptions (Architecture.md §12).
template <typename T>
class Result
{
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(Error error) { return Result(std::move(error)); }

    bool hasValue() const noexcept { return std::holds_alternative<T>(m_value); }
    bool hasError() const noexcept { return !hasValue(); }
    explicit operator bool() const noexcept { return hasValue(); }

    const T& value() const& { return std::get<T>(m_value); }
    T& value() & { return std::get<T>(m_value); }
    T&& value() && { return std::get<T>(std::move(m_value)); }

    const Error& error() const& { return std::get<Error>(m_value); }
    Error&& error() && { return std::get<Error>(std::move(m_value)); }

private:
    explicit Result(T value)
        : m_value(std::move(value))
    {
    }

    explicit Result(Error error)
        : m_value(std::move(error))
    {
    }

    std::variant<T, Error> m_value;
};

template <>
class Result<void>
{
public:
    static Result success() { return Result(); }
    static Result failure(Error error) { return Result(std::move(error)); }

    bool hasValue() const noexcept { return !m_error.has_value(); }
    bool hasError() const noexcept { return m_error.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }

    const Error& error() const& { return *m_error; }
    Error&& error() && { return std::move(*m_error); }

private:
    Result() = default;

    explicit Result(Error error)
        : m_error(std::move(error))
    {
    }

    std::optional<Error> m_error;
};
