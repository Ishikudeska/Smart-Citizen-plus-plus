#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// .NET behaviours sc.gamedata's output depends on: number parsing and
// rounding, string trimming, List<T>.Sort's introsort, and culture-aware
// (ICU root) string ordering.
namespace engine::gamedata::dotnet {

// Double.TryParse(s, NumberStyles.Float, InvariantCulture).
std::optional<double> parseDouble(std::string_view s);
// Int32.TryParse(s, NumberStyles.Integer, InvariantCulture).
std::optional<std::int32_t> parseInt32(std::string_view s);

// Math.Round(value, decimals): half to even, via value * 10^d.
double round(double value, int decimals);

// Char.IsWhiteSpace for one code point.
bool isWhiteSpace(char32_t c);
// String.Trim() / TrimEnd() over UTF-8.
std::string_view trim(std::string_view s);
std::string_view trimEnd(std::string_view s);

// String.Compare(a, b, StringComparison.Ordinal) over UTF-8 (UTF-16 code
// unit order).
int compareOrdinal(std::string_view a, std::string_view b);

// String.Compare(a, b) under the invariant culture: ICU root collation at
// tertiary strength, as .NET 5+ uses on Windows (icu.dll). Falls back to a
// case-insensitive-then-ordinal order where ICU is unavailable.
int compareCulture(std::string_view a, std::string_view b);

// List<T>.Sort(Comparison<T>): ArraySortHelper's introspective sort, whose
// order for equal elements the output depends on.
template <class T, class Cmp>
void introSort(std::vector<T> &items, Cmp cmp);

// DateTime.UtcNow.ToString("o").
std::string utcNowRoundTrip();

// ── implementation of introSort ──────────────────────────────────────────

namespace detail {

template <class T, class Cmp>
void swapIfGreater(std::vector<T> &k, Cmp &cmp, std::size_t i, std::size_t j)
{
    if (cmp(k[i], k[j]) > 0)
        std::swap(k[i], k[j]);
}

template <class T, class Cmp>
void insertionSort(std::vector<T> &k, Cmp &cmp, std::size_t lo, std::size_t n)
{
    for (std::size_t i = 0; i + 1 < n; ++i) {
        T t = std::move(k[lo + i + 1]);
        std::ptrdiff_t j = static_cast<std::ptrdiff_t>(i);
        while (j >= 0 && cmp(t, k[lo + j]) < 0) {
            k[lo + j + 1] = std::move(k[lo + j]);
            --j;
        }
        k[lo + j + 1] = std::move(t);
    }
}

template <class T, class Cmp>
void downHeap(std::vector<T> &k, Cmp &cmp, std::size_t lo, std::size_t i, std::size_t n)
{
    T d = std::move(k[lo + i - 1]);
    while (i <= n / 2) {
        std::size_t child = 2 * i;
        if (child < n && cmp(k[lo + child - 1], k[lo + child]) < 0)
            ++child;
        if (!(cmp(d, k[lo + child - 1]) < 0))
            break;
        k[lo + i - 1] = std::move(k[lo + child - 1]);
        i = child;
    }
    k[lo + i - 1] = std::move(d);
}

template <class T, class Cmp>
void heapSort(std::vector<T> &k, Cmp &cmp, std::size_t lo, std::size_t n)
{
    for (std::size_t i = n / 2; i >= 1; --i)
        downHeap(k, cmp, lo, i, n);
    for (std::size_t i = n; i > 1; --i) {
        std::swap(k[lo], k[lo + i - 1]);
        downHeap(k, cmp, lo, 1, i - 1);
    }
}

template <class T, class Cmp>
std::size_t pickPivotAndPartition(std::vector<T> &k, Cmp &cmp, std::size_t lo, std::size_t n)
{
    const std::size_t hi = n - 1;
    const std::size_t middle = hi >> 1;
    swapIfGreater(k, cmp, lo, lo + middle);
    swapIfGreater(k, cmp, lo, lo + hi);
    swapIfGreater(k, cmp, lo + middle, lo + hi);
    T pivot = k[lo + middle];
    std::swap(k[lo + middle], k[lo + hi - 1]);
    std::size_t left = 0, right = hi - 1;
    while (left < right) {
        while (cmp(k[lo + ++left], pivot) < 0) {
        }
        while (cmp(pivot, k[lo + --right]) < 0) {
        }
        if (left >= right)
            break;
        std::swap(k[lo + left], k[lo + right]);
    }
    if (left != hi - 1)
        std::swap(k[lo + left], k[lo + hi - 1]);
    return left;
}

template <class T, class Cmp>
void introSortRange(std::vector<T> &k, Cmp &cmp, std::size_t lo, std::size_t n, int depthLimit)
{
    std::size_t partitionSize = n;
    while (partitionSize > 1) {
        if (partitionSize <= 16) {
            if (partitionSize == 2) {
                swapIfGreater(k, cmp, lo, lo + 1);
                return;
            }
            if (partitionSize == 3) {
                swapIfGreater(k, cmp, lo, lo + 1);
                swapIfGreater(k, cmp, lo, lo + 2);
                swapIfGreater(k, cmp, lo + 1, lo + 2);
                return;
            }
            insertionSort(k, cmp, lo, partitionSize);
            return;
        }
        if (depthLimit == 0) {
            heapSort(k, cmp, lo, partitionSize);
            return;
        }
        --depthLimit;
        const std::size_t p = pickPivotAndPartition(k, cmp, lo, partitionSize);
        introSortRange(k, cmp, lo + p + 1, partitionSize - (p + 1), depthLimit);
        partitionSize = p;
    }
}

} // namespace detail

template <class T, class Cmp>
void introSort(std::vector<T> &items, Cmp cmp)
{
    if (items.size() < 2)
        return;
    int log2 = 0;
    for (std::size_t n = items.size(); n > 1; n >>= 1)
        ++log2;
    detail::introSortRange(items, cmp, 0, items.size(), 2 * (log2 + 1));
}

} // namespace engine::gamedata::dotnet
