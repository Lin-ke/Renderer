#ifndef UUID_H
#define UUID_H

#include <uuid.h>
#include <string>

class UID {
public:
	UID();
	explicit UID(const std::string &str);
	static UID empty();
	static UID from_hash(const std::string &str);
	const std::string &to_string() const { return str; }
	const uuids::uuid &get_raw() const { return id; }
	bool operator==(const UID &other) const { return id == other.id; }
	bool operator!=(const UID &other) const { return id != other.id; }
	bool is_empty() const { return id.is_nil(); }

private:
	// 私有构造：用于 Empty()，避免不必要的随机生成
	struct NullTag {};
	explicit UID(NullTag);

private:
	uuids::uuid id;
	std::string str;

public:
    template <class Archive>
    std::string save_minimal(const Archive&) const {
        return str;
    }

    template <class Archive>
    void load_minimal(const Archive&, const std::string& value) {
        auto parsed = uuids::uuid::from_string(value);
        if (parsed) {
            id = *parsed;
        } else {
            id = uuids::uuid(); // nil
        }
        str = value;
    }	

	void write(std::ostream &os, bool is_binary) const {
		if (is_binary) {
			os.write(reinterpret_cast<const char *>(&id), sizeof(uuids::uuid));
		} else {
			os.write(str.c_str(), str.size());
		}
	}
	void read(std::istream &is, bool is_binary) {
		if (is_binary) {
			is.read(reinterpret_cast<char *>(&id), sizeof(uuids::uuid));
			str = uuids::to_string(id);
		} else {
			char buffer[37] = { 0 };
			is.read(buffer, 36);
			str = buffer;
			auto parsed_id = uuids::uuid::from_string(buffer);
			if (parsed_id.has_value()) {
				id = parsed_id.value();
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

#endif