#pragma once

#include <uuid.h>
#include <string>

class UID {
public:
	UID();
	explicit UID(const std::string &str);
	static UID empty();
	static UID generate();
	static UID from_hash(const std::string &str);
	const std::string &to_string() const { return str_; }
	const uuids::uuid &get_raw() const { return id_; }
	bool operator==(const UID &other) const { return id_ == other.id_; }
	bool operator!=(const UID &other) const { return id_ != other.id_; }
	bool is_empty() const { return id_.is_nil(); }

private:
	// 私有构造：用于 Empty()，避免不必要的随机生成
	struct NullTag {};
	explicit UID(NullTag);

private:
	uuids::uuid id_;
	std::string str_;

public:
    template <class Archive>
    std::string save_minimal(const Archive&) const {
        return str_;
    }

    template <class Archive>
    void load_minimal(const Archive&, const std::string& value) {
        auto parsed = uuids::uuid::from_string(value);
        if (parsed) {
            id_ = *parsed;
        } else {
            id_ = uuids::uuid(); // nil
        }
        str_ = value;
    }	

	void write(std::ostream &os, bool is_binary) const {
		if (is_binary) {
			os.write(reinterpret_cast<const char *>(&id_), sizeof(uuids::uuid));
		} else {
			os.write(str_.c_str(), str_.size());
		}
	}
	void read(std::istream &is, bool is_binary) {
		if (is_binary) {
			is.read(reinterpret_cast<char *>(&id_), sizeof(uuids::uuid));
			str_ = uuids::to_string(id_);
		} else {
			char buffer[37] = { 0 };
			is.read(buffer, 36);
			str_ = buffer;
			auto parsed_id = uuids::uuid::from_string(buffer);
			if (parsed_id.has_value()) {
				id_ = parsed_id.value();
			}
		}
	}
};

namespace std {
template <>
struct hash<UID> {
	size_t operator()(const UID &uid) const {
		return std::hash<uuids::uuid>{}(uid.get_raw());
	}
};
} //namespace std