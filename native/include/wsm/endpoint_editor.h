#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace wsm {

enum class EndpointKind { Address, Port };
enum class EditKey { Left, Right, Home, End, Backspace, Delete, SelectAll };

class EndpointEditor {
public:
    explicit EndpointEditor(EndpointKind kind) : kind_(kind) {}

    // Reset selects the entire valid value; failed resets preserve all state.
    bool begin(std::string_view value) {
        if (value.size() > limit() || !valid(value)) return false;
        text_ = std::string(value);
        anchor_ = 0;
        cursor_ = text_.size();
        return true;
    }

    bool character(char32_t ch) {
        if (!allowed(ch)) return false;
        const char value = static_cast<char>(ch);
        return paste(std::string_view(&value, 1));
    }

    bool paste(std::string_view value) {
        const auto lo = (std::min)(anchor_, cursor_);
        const auto hi = (std::max)(anchor_, cursor_);
        if (!valid(value) || value.size() > limit() - (text_.size() - (hi - lo))) return false;
        if (value.empty() && lo == hi) return false;
        // Copy first: the supplied view may refer to text_ itself.
        const std::string insertion(value);
        text_.replace(lo, hi - lo, insertion);
        cursor_ = anchor_ = lo + insertion.size();
        return true;
    }

    bool key(EditKey key, bool shift = false, bool control = false) {
        const auto old_cursor = cursor_;
        const auto old_anchor = anchor_;
        const auto lo = (std::min)(anchor_, cursor_);
        const auto hi = (std::max)(anchor_, cursor_);
        if (key == EditKey::SelectAll) {
            anchor_ = 0;
            cursor_ = text_.size();
        } else if (key == EditKey::Backspace || key == EditKey::Delete) {
            std::size_t first = lo, last = hi;
            if (lo == hi) {
                if (key == EditKey::Backspace) first = control ? word_left(cursor_) : (cursor_ ? cursor_ - 1 : 0);
                else last = control ? word_right(cursor_) : (std::min)(cursor_ + 1, text_.size());
            }
            if (first == last) return false;
            text_.erase(first, last - first);
            anchor_ = cursor_ = first;
            return true;
        } else {
            std::size_t next = cursor_;
            switch (key) {
            case EditKey::Left:
                next = !shift && lo != hi ? lo : control ? word_left(cursor_) : (cursor_ ? cursor_ - 1 : 0);
                break;
            case EditKey::Right:
                next = !shift && lo != hi ? hi : control ? word_right(cursor_) : (std::min)(cursor_ + 1, text_.size());
                break;
            case EditKey::Home: next = 0; break;
            case EditKey::End: next = text_.size(); break;
            default: return false;
            }
            cursor_ = next;
            if (!shift) anchor_ = cursor_;
        }
        return old_cursor != cursor_ || old_anchor != anchor_;
    }

    const std::string& text() const { return text_; }
    std::size_t cursor() const { return cursor_; }
    std::size_t anchor() const { return anchor_; }

private:
    std::size_t limit() const { return kind_ == EndpointKind::Address ? 15 : 5; }
    bool allowed(char32_t c) const {
        return (c >= U'0' && c <= U'9') || (kind_ == EndpointKind::Address && c == U'.');
    }
    bool valid(std::string_view value) const {
        for (unsigned char c : value) if (!allowed(c)) return false;
        return true;
    }
    // Ctrl movement/deletion skips dots, then the adjacent digit component;
    // for ports the complete digit sequence is one component.
    std::size_t word_left(std::size_t pos) const {
        while (pos && text_[pos - 1] == '.') --pos;
        while (pos && text_[pos - 1] != '.') --pos;
        return pos;
    }
    std::size_t word_right(std::size_t pos) const {
        while (pos < text_.size() && text_[pos] == '.') ++pos;
        while (pos < text_.size() && text_[pos] != '.') ++pos;
        return pos;
    }

    EndpointKind kind_;
    std::string text_;
    std::size_t cursor_ = 0;
    std::size_t anchor_ = 0;
};

} // namespace wsm
