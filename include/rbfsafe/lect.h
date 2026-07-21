#pragma once

#include <rbfsafe/result.h>
#include <rbfsafe/types.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rbfsafe {

class LectNodeKey {
  public:
    LectNodeKey() = default;
    explicit LectNodeKey(std::string path) : path_(std::move(path)) {}

    static LectNodeKey root() { return LectNodeKey(""); }
    const std::string& path() const noexcept { return path_; }
    std::size_t depth() const noexcept { return path_.size(); }
    std::string to_string() const { return path_.empty() ? "root" : path_; }
    bool valid() const noexcept;

    friend bool operator==(const LectNodeKey&, const LectNodeKey&) = default;
    friend bool operator<(const LectNodeKey& lhs, const LectNodeKey& rhs) { return lhs.path_ < rhs.path_; }

  private:
    std::string path_;
};

enum class SplitStrategy : std::uint8_t { NormalizedLongestAxis = 0 };

struct SplitPolicy {
    SplitStrategy strategy = SplitStrategy::NormalizedLongestAxis;
    double minimum_normalized_width = 1e-3;
};

struct LectNode {
    LectNodeKey key;
    CspaceAabb box;
    bool leaf = true;
    std::size_t split_dimension = 0;
    LectNodeKey left;
    LectNodeKey right;
};

class LectTree {
  public:
    LectTree() = default;

    static Result<LectTree> create(CspaceAabb root, SplitPolicy policy = {});
    static Result<LectTree> restore(CspaceAabb root, SplitPolicy policy, std::vector<LectNode> nodes);

    const CspaceAabb& root_domain() const noexcept { return root_; }
    const SplitPolicy& policy() const noexcept { return policy_; }
    std::size_t size() const noexcept { return nodes_.size(); }
    LectNodeKey root_key() const { return LectNodeKey::root(); }

    Result<LectNode> node(const LectNodeKey& key) const;
    Result<std::pair<LectNodeKey, LectNodeKey>> split(const LectNodeKey& key);
    Result<LectNodeKey> locate(std::span<const double> configuration) const;
    Result<std::vector<LectNodeKey>> overlap_leaves(const CspaceAabb& box) const;
    std::vector<LectNodeKey> leaf_keys() const;
    std::vector<LectNode> all_nodes() const;

    Result<void> save(const std::filesystem::path& directory) const;

  private:
    Result<std::size_t> index_of(const LectNodeKey& key) const;
    std::size_t choose_split_dimension(const CspaceAabb& box) const;

    CspaceAabb root_;
    SplitPolicy policy_;
    std::vector<LectNode> nodes_;
    std::unordered_map<std::string, std::size_t> indices_;
};

class LectSnapshot {
  public:
    LectSnapshot() = default;
    explicit LectSnapshot(std::shared_ptr<const LectTree> tree) : tree_(std::move(tree)) {}

    static LectSnapshot from_tree(LectTree tree);
    static Result<LectSnapshot> open(const std::filesystem::path& directory);

    bool valid() const noexcept { return static_cast<bool>(tree_); }
    const CspaceAabb& root_domain() const;
    const SplitPolicy& policy() const;
    std::size_t size() const noexcept;
    Result<LectNode> node(const LectNodeKey& key) const;
    Result<LectNodeKey> locate(std::span<const double> configuration) const;
    Result<std::vector<LectNodeKey>> overlap_leaves(const CspaceAabb& box) const;
    std::vector<LectNodeKey> leaf_keys() const;
    std::vector<LectNode> all_nodes() const;

  private:
    std::shared_ptr<const LectTree> tree_;
};

} // namespace rbfsafe
