#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/core/hash/murmur_hash.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

class RDGBufferPool {
public:
    struct PooledBuffer {
        RHIBufferRef buffer;
        RHIResourceState state;
    };

    struct Key {
        Key(const RHIBufferInfo& info) : memory_usage(info.memory_usage), type(info.type), creation_flag(info.creation_flag) {}

        MemoryUsage memory_usage;
        ResourceType type;
        BufferCreationFlags creation_flag;

        friend bool operator==(const Key& a, const Key& b) {
            return a.memory_usage == b.memory_usage && a.type == b.type && a.creation_flag == b.creation_flag;
        }

        struct Hash {
            size_t operator()(const Key& a) const { return MurmurHash64A(&a, sizeof(Key), 0); }
        };
    };

    PooledBuffer allocate(const RHIBufferInfo& info);
    void release(const PooledBuffer& pooled_buffer);

    inline uint32_t pooled_size() { return pooled_size_; }
    inline uint32_t allocated_size() { return allocated_size_; }
    void clear() {
        pooled_buffers_.clear();
        pooled_size_ = 0;
    }

    static std::shared_ptr<RDGBufferPool> get() {
        static std::shared_ptr<RDGBufferPool> pool;
        if (pool == nullptr) pool = std::make_shared<RDGBufferPool>();
        return pool;
    }

private:
    std::unordered_map<Key, std::list<PooledBuffer>, Key::Hash> pooled_buffers_;
    uint32_t pooled_size_ = 0;
    uint32_t allocated_size_ = 0;
};

class RDGTexturePool {
public:
    struct PooledTexture {
        RHITextureRef texture;
        RHIResourceState state;
    };

    struct Key {
        Key(const RHITextureInfo& info) : info(info) {}

        RHITextureInfo info;

        friend bool operator==(const Key& a, const Key& b) { return a.info == b.info; }

        struct Hash {
            size_t operator()(const Key& a) const { return MurmurHash64A(&a, sizeof(Key), 0); }
        };
    };

    PooledTexture allocate(const RHITextureInfo& info);
    void release(const PooledTexture& pooled_texture);

    inline uint32_t pooled_size() { return pooled_size_; }
    inline uint32_t allocated_size() { return allocated_size_; }
    void clear() {
        pooled_textures_.clear();
        pooled_size_ = 0;
    }

    static std::shared_ptr<RDGTexturePool> get() {
        static std::shared_ptr<RDGTexturePool> pool;
        if (pool == nullptr) pool = std::make_shared<RDGTexturePool>();
        return pool;
    }

private:
    std::unordered_map<Key, std::list<PooledTexture>, Key::Hash> pooled_textures_;
    uint32_t pooled_size_ = 0;
    uint32_t allocated_size_ = 0;
};

class RDGTextureViewPool {
public:
    struct PooledTextureView {
        RHITextureViewRef texture_view;
    };

    struct Key {
        Key(const RHITextureViewInfo& info) : info(info) {}

        RHITextureViewInfo info;

        friend bool operator==(const Key& a, const Key& b) { return a.info == b.info; }

        struct Hash {
            size_t operator()(const Key& a) const { return MurmurHash64A(&a.info, sizeof(RHITextureViewInfo), 0); }
        };
    };

    PooledTextureView allocate(const RHITextureViewInfo& info);
    void release(const PooledTextureView& pooled_texture_view);

    inline uint32_t pooled_size() { return pooled_size_; }
    inline uint32_t allocated_size() { return allocated_size_; }
    void clear() {
        pooled_texture_views_.clear();
        pooled_size_ = 0;
    }

    static std::shared_ptr<RDGTextureViewPool> get() {
        static std::shared_ptr<RDGTextureViewPool> pool;
        if (pool == nullptr) pool = std::make_shared<RDGTextureViewPool>();
        return pool;
    }

private:
    std::unordered_map<Key, std::list<PooledTextureView>, Key::Hash> pooled_texture_views_;
    uint32_t pooled_size_ = 0;
    uint32_t allocated_size_ = 0;
};

class RDGDescriptorSetPool {
public:
    struct PooledDescriptor {
        RHIDescriptorSetRef descriptor;
    };

    struct Key {
        Key(const RHIRootSignatureInfo& info, uint32_t set) : entries(info.get_entries()), set(set) {}

        std::vector<ShaderResourceEntry> entries;
        uint32_t set;

        friend bool operator==(const Key& a, const Key& b) { return a.entries == b.entries && a.set == b.set; }

        struct HashEntries {
            size_t operator()(std::vector<ShaderResourceEntry> entries) const {
                return MurmurHash64A(entries.data(), entries.size() * sizeof(ShaderResourceEntry), 0);
            }
        };

        struct Hash {
            size_t operator()(const Key& a) const { return std::hash<uint32_t>()(a.set) ^ (HashEntries()(a.entries) << 1); }
        };
    };

    PooledDescriptor allocate(const RHIRootSignatureRef& root_signature, uint32_t set);
    void release(const PooledDescriptor& pooled_descriptor, const RHIRootSignatureRef& root_signature, uint32_t set);

    inline uint32_t pooled_size() { return pooled_size_; }
    inline uint32_t allocated_size() { return allocated_size_; }
    void clear() {
        pooled_descriptors_.clear();
        pooled_size_ = 0;
    }

    static std::shared_ptr<RDGDescriptorSetPool> get(uint32_t index) {
        static std::shared_ptr<RDGDescriptorSetPool> pool[3];
        if (pool[index] == nullptr) pool[index] = std::make_shared<RDGDescriptorSetPool>();
        return pool[index];
    }

private:
    std::unordered_map<Key, std::list<PooledDescriptor>, Key::Hash> pooled_descriptors_;
    uint32_t pooled_size_ = 0;
    uint32_t allocated_size_ = 0;
};
