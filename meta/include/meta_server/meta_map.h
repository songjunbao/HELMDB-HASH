#pragma once
#include <iostream>
#include <type_traits>
#include <unordered_map>
#include <initializer_list>
#include <string>
#include <typeinfo>

namespace meta {
template<typename K, typename V>
class MetaMap {
public:
    using iterator = typename std::unordered_map<K, V>::iterator;
    using const_iterator = typename std::unordered_map<K, V>::const_iterator;

    MetaMap();
    MetaMap(const std::unordered_map<K, V>& map);
    MetaMap(std::initializer_list<std::pair<const K, V>> initList);
    ~MetaMap();

    // 插入元素
    void insert(const K& key, const V& value);

    // 访问元素
    V at(const K& key) const;
    V& operator[](const K& key);

    // 删除元素
    void erase(const K& key);

    // 查找元素
    iterator find(const K& key);
    const_iterator find(const K& key) const;

    // 获取大小
    size_t size() const;

    // 清空 map
    void clear();

    // 迭代器接口
    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;

private:
    std::unordered_map<K, V> local_map;
};

// 构造函数
template<typename K, typename V>
MetaMap<K, V>::MetaMap() {
    // 实现 Braft 节点的初始化，省略细节
}

// 用已有的 unordered_map 构造
template<typename K, typename V>
MetaMap<K, V>::MetaMap(const std::unordered_map<K, V>& map) : MetaMap() {
    for (const auto& pair : map) {
        insert(pair.first, pair.second);
    }
}

// 支持初始化列表的构造函数
template<typename K, typename V>
MetaMap<K, V>::MetaMap(std::initializer_list<std::pair<const K, V>> initList) : MetaMap() {
    for (const auto& pair : initList) {
        insert(pair.first, pair.second);
    }
}

// 析构函数
template<typename K, typename V>
MetaMap<K, V>::~MetaMap() {
    // 实现 Braft 节点的关闭，省略细节
}

// 插入元素
template<typename K, typename V>
void MetaMap<K, V>::insert(const K& key, const V& value) {
    local_map[key] = value;
}

// 访问元素
template<typename K, typename V>
V MetaMap<K, V>::at(const K& key) const {
    return local_map.at(key);
}

template<typename K, typename V>
V& MetaMap<K, V>::operator[](const K& key) {
    return local_map[key];
}

// 删除元素
template<typename K, typename V>
void MetaMap<K, V>::erase(const K& key) {
    local_map.erase(key);
}

// 查找元素
template<typename K, typename V>
typename MetaMap<K, V>::iterator MetaMap<K, V>::find(const K& key) {
    return local_map.find(key);
}

template<typename K, typename V>
typename MetaMap<K, V>::const_iterator MetaMap<K, V>::find(const K& key) const {
    return local_map.find(key);
}

// 获取大小
template<typename K, typename V>
size_t MetaMap<K, V>::size() const {
    return local_map.size();
}

// 清空 map
template<typename K, typename V>
void MetaMap<K, V>::clear() {
    local_map.clear();
}

// 迭代器接口
template<typename K, typename V>
typename MetaMap<K, V>::iterator MetaMap<K, V>::begin() {
    return local_map.begin();
}

template<typename K, typename V>
typename MetaMap<K, V>::const_iterator MetaMap<K, V>::begin() const {
    return local_map.begin();
}

template<typename K, typename V>
typename MetaMap<K, V>::iterator MetaMap<K, V>::end() {
    return local_map.end();
}

template<typename K, typename V>
typename MetaMap<K, V>::const_iterator MetaMap<K, V>::end() const {
    return local_map.end();
}
}