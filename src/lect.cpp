#include <rbfsafe/lect.h>
#include <rbfsafe/version.h>

#include "internal/binary.h"
#include "internal/json.h"
#include "internal/lect_codec.h"
#include "internal/sha256.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>

namespace rbfsafe {
namespace {

constexpr std::string_view kLectMagic = "RBFLECT1";
constexpr std::uint64_t kMaximumStoredNodes = 10'000'000;
constexpr std::uint32_t kMaximumDimension = 64;

Result<void> validate_policy(const SplitPolicy& policy) {
    if (policy.strategy != SplitStrategy::NormalizedLongestAxis) {
        return Result<void>::failure(StatusCode::InvalidArgument, "unsupported LECT split strategy");
    }
    if (!std::isfinite(policy.minimum_normalized_width) || policy.minimum_normalized_width < 0.0 ||
        policy.minimum_normalized_width >= 1.0) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "minimum normalized width must be in [0, 1)");
    }
    return Result<void>::success();
}

} // namespace

namespace internal {

BinaryWriter encode_lect_tree(const LectTree& tree) {
    internal::BinaryWriter writer;
    writer.text(kLectMagic);
    writer.u32(kLectSchemaVersion);
    writer.u32(static_cast<std::uint32_t>(tree.root_domain().dimension()));
    const auto nodes = tree.all_nodes();
    writer.u64(static_cast<std::uint64_t>(nodes.size()));
    writer.u8(static_cast<std::uint8_t>(tree.policy().strategy));
    writer.f64(tree.policy().minimum_normalized_width);
    for (const auto& axis : tree.root_domain().axes()) {
        writer.f64(axis.lower);
        writer.f64(axis.upper);
    }
    for (const auto& node : nodes) {
        writer.string(node.key.path());
        writer.u8(node.leaf ? 1u : 0u);
        writer.u32(static_cast<std::uint32_t>(node.split_dimension));
        for (const auto& axis : node.box.axes()) {
            writer.f64(axis.lower);
            writer.f64(axis.upper);
        }
    }
    return writer;
}

Result<LectTree> decode_lect_tree(std::span<const std::byte> bytes) {
    BinaryReader reader(bytes);
    auto magic = reader.fixed_text(kLectMagic.size());
    if (!magic)
        return magic.error();
    if (magic.value() != kLectMagic)
        return Result<LectTree>::failure(StatusCode::IncompatibleFormat, "invalid LECT magic");
    auto schema = reader.u32();
    if (!schema)
        return schema.error();
    if (schema.value() != kLectSchemaVersion)
        return Result<LectTree>::failure(StatusCode::IncompatibleFormat, "unsupported LECT schema");
    auto dimension = reader.u32();
    if (!dimension)
        return dimension.error();
    if (dimension.value() == 0 || dimension.value() > kMaximumDimension) {
        return Result<LectTree>::failure(StatusCode::CorruptData, "invalid LECT dimension");
    }
    auto node_count = reader.u64();
    if (!node_count)
        return node_count.error();
    if (node_count.value() == 0 || node_count.value() > kMaximumStoredNodes) {
        return Result<LectTree>::failure(StatusCode::ResourceLimit, "invalid LECT node count");
    }
    auto strategy = reader.u8();
    if (!strategy)
        return strategy.error();
    auto minimum_width = reader.f64();
    if (!minimum_width)
        return minimum_width.error();
    SplitPolicy policy{static_cast<SplitStrategy>(strategy.value()), minimum_width.value()};
    std::vector<Interval> root_axes;
    root_axes.reserve(dimension.value());
    for (std::uint32_t axis = 0; axis < dimension.value(); ++axis) {
        auto lower = reader.f64();
        if (!lower)
            return lower.error();
        auto upper = reader.f64();
        if (!upper)
            return upper.error();
        root_axes.emplace_back(lower.value(), upper.value());
    }
    CspaceAabb root(std::move(root_axes));
    std::vector<LectNode> nodes;
    nodes.reserve(static_cast<std::size_t>(node_count.value()));
    for (std::uint64_t index = 0; index < node_count.value(); ++index) {
        auto path = reader.string(4096);
        if (!path)
            return path.error();
        auto leaf = reader.u8();
        if (!leaf)
            return leaf.error();
        auto split_dimension = reader.u32();
        if (!split_dimension)
            return split_dimension.error();
        std::vector<Interval> axes;
        axes.reserve(dimension.value());
        for (std::uint32_t axis = 0; axis < dimension.value(); ++axis) {
            auto lower = reader.f64();
            if (!lower)
                return lower.error();
            auto upper = reader.f64();
            if (!upper)
                return upper.error();
            axes.emplace_back(lower.value(), upper.value());
        }
        LectNodeKey key(path.value());
        LectNode node{key,
                      CspaceAabb(std::move(axes)),
                      leaf.value() != 0u,
                      static_cast<std::size_t>(split_dimension.value()),
                      LectNodeKey(path.value() + "0"),
                      LectNodeKey(path.value() + "1")};
        nodes.push_back(std::move(node));
    }
    if (!reader.finished())
        return Result<LectTree>::failure(StatusCode::CorruptData, "LECT payload has trailing bytes");
    return LectTree::restore(std::move(root), policy, std::move(nodes));
}

} // namespace internal

namespace {

std::filesystem::path temporary_sibling(const std::filesystem::path& destination) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() / (destination.filename().string() + ".tmp-" + std::to_string(nonce));
}

} // namespace

bool LectNodeKey::valid() const noexcept {
    return std::all_of(path_.begin(), path_.end(), [](char value) { return value == '0' || value == '1'; });
}

Result<LectTree> LectTree::create(CspaceAabb root, SplitPolicy policy) {
    if (!root.valid() || root.dimension() > kMaximumDimension) {
        return Result<LectTree>::failure(StatusCode::InvalidArgument, "LECT root domain is invalid");
    }
    auto policy_status = validate_policy(policy);
    if (!policy_status)
        return policy_status.error();
    for (const auto& axis : root.axes()) {
        if (axis.width() <= 0.0)
            return Result<LectTree>::failure(StatusCode::InvalidArgument,
                                             "LECT root axes must have positive width");
    }
    LectTree tree;
    tree.root_ = std::move(root);
    tree.policy_ = policy;
    tree.nodes_.push_back(
        LectNode{LectNodeKey::root(), tree.root_, true, 0, LectNodeKey("0"), LectNodeKey("1")});
    tree.indices_.emplace("", 0);
    return tree;
}

Result<LectTree> LectTree::restore(CspaceAabb root, SplitPolicy policy, std::vector<LectNode> nodes) {
    auto created = create(std::move(root), policy);
    if (!created)
        return created;
    if (nodes.empty() || nodes.front().key != LectNodeKey::root()) {
        return Result<LectTree>::failure(StatusCode::CorruptData, "LECT node table has no root");
    }
    LectTree tree;
    tree.root_ = created.value().root_;
    tree.policy_ = policy;
    tree.nodes_ = std::move(nodes);
    for (std::size_t index = 0; index < tree.nodes_.size(); ++index) {
        auto& node = tree.nodes_[index];
        if (!node.key.valid() || !node.box.valid() || node.box.dimension() != tree.root_.dimension()) {
            return Result<LectTree>::failure(StatusCode::CorruptData, "invalid LECT node",
                                             node.key.to_string());
        }
        if (!tree.indices_.emplace(node.key.path(), index).second) {
            return Result<LectTree>::failure(StatusCode::CorruptData, "duplicate LECT node key",
                                             node.key.to_string());
        }
        node.left = LectNodeKey(node.key.path() + "0");
        node.right = LectNodeKey(node.key.path() + "1");
        if (!node.leaf && node.split_dimension >= tree.root_.dimension()) {
            return Result<LectTree>::failure(StatusCode::CorruptData, "invalid LECT split dimension",
                                             node.key.to_string());
        }
    }
    if (tree.nodes_.front().box.axes() != tree.root_.axes()) {
        return Result<LectTree>::failure(StatusCode::CorruptData,
                                         "LECT root node does not match root domain");
    }
    for (const auto& node : tree.nodes_) {
        if (!node.leaf &&
            (tree.indices_.count(node.left.path()) == 0 || tree.indices_.count(node.right.path()) == 0)) {
            return Result<LectTree>::failure(StatusCode::CorruptData, "LECT branch is missing a child",
                                             node.key.to_string());
        }
    }
    return tree;
}

Result<std::size_t> LectTree::index_of(const LectNodeKey& key) const {
    if (!key.valid())
        return Result<std::size_t>::failure(StatusCode::InvalidArgument, "invalid LECT node key");
    const auto iterator = indices_.find(key.path());
    if (iterator == indices_.end())
        return Result<std::size_t>::failure(StatusCode::InvalidArgument, "LECT node key does not exist",
                                            key.to_string());
    return iterator->second;
}

Result<LectNode> LectTree::node(const LectNodeKey& key) const {
    auto index = index_of(key);
    if (!index)
        return index.error();
    return nodes_[index.value()];
}

std::size_t LectTree::choose_split_dimension(const CspaceAabb& box) const {
    std::size_t selected = 0;
    double selected_width = -1.0;
    for (std::size_t dimension = 0; dimension < box.dimension(); ++dimension) {
        const double normalized = box.axes()[dimension].width() / root_.axes()[dimension].width();
        if (normalized > selected_width) {
            selected = dimension;
            selected_width = normalized;
        }
    }
    return selected;
}

Result<std::pair<LectNodeKey, LectNodeKey>> LectTree::split(const LectNodeKey& key) {
    auto index_result = index_of(key);
    if (!index_result)
        return index_result.error();
    const std::size_t index = index_result.value();
    if (!nodes_[index].leaf)
        return std::pair{nodes_[index].left, nodes_[index].right};
    const std::size_t dimension = choose_split_dimension(nodes_[index].box);
    const double normalized_width =
        nodes_[index].box.axes()[dimension].width() / root_.axes()[dimension].width();
    if (normalized_width <= policy_.minimum_normalized_width) {
        return Result<std::pair<LectNodeKey, LectNodeKey>>::failure(
            StatusCode::ResourceLimit, "LECT node reached minimum normalized width", key.to_string());
    }
    const double midpoint = nodes_[index].box.axes()[dimension].center();
    if (!(midpoint > nodes_[index].box.axes()[dimension].lower &&
          midpoint < nodes_[index].box.axes()[dimension].upper)) {
        return Result<std::pair<LectNodeKey, LectNodeKey>>::failure(
            StatusCode::ResourceLimit, "LECT node cannot be split numerically", key.to_string());
    }
    auto left_box = nodes_[index].box;
    auto right_box = nodes_[index].box;
    left_box.axes()[dimension].upper = midpoint;
    right_box.axes()[dimension].lower = midpoint;
    const LectNodeKey left(key.path() + "0");
    const LectNodeKey right(key.path() + "1");
    const std::size_t left_index = nodes_.size();
    nodes_.push_back(LectNode{left, std::move(left_box), true, 0, LectNodeKey(left.path() + "0"),
                              LectNodeKey(left.path() + "1")});
    const std::size_t right_index = nodes_.size();
    nodes_.push_back(LectNode{right, std::move(right_box), true, 0, LectNodeKey(right.path() + "0"),
                              LectNodeKey(right.path() + "1")});
    indices_.emplace(left.path(), left_index);
    indices_.emplace(right.path(), right_index);
    nodes_[index].leaf = false;
    nodes_[index].split_dimension = dimension;
    nodes_[index].left = left;
    nodes_[index].right = right;
    return std::pair{left, right};
}

Result<LectNodeKey> LectTree::locate(std::span<const double> configuration) const {
    auto status = validate_configuration(configuration, root_.dimension());
    if (!status)
        return status.error();
    if (!root_.contains(configuration, 1e-12)) {
        return Result<LectNodeKey>::failure(StatusCode::InvalidArgument,
                                            "configuration lies outside LECT root");
    }
    std::size_t index = 0;
    while (!nodes_[index].leaf) {
        const auto& current = nodes_[index];
        const double midpoint = current.box.axes()[current.split_dimension].center();
        const auto next_key =
            configuration[current.split_dimension] <= midpoint ? current.left : current.right;
        auto next = index_of(next_key);
        if (!next)
            return Result<LectNodeKey>::failure(StatusCode::CorruptData, "LECT branch is incomplete");
        index = next.value();
    }
    return nodes_[index].key;
}

Result<std::vector<LectNodeKey>> LectTree::overlap_leaves(const CspaceAabb& box) const {
    if (!box.valid())
        return Result<std::vector<LectNodeKey>>::failure(StatusCode::InvalidArgument,
                                                         "overlap query box is invalid");
    if (box.dimension() != root_.dimension())
        return Result<std::vector<LectNodeKey>>::failure(StatusCode::DimensionMismatch,
                                                         "overlap query dimension does not match LECT");
    std::vector<LectNodeKey> result;
    for (const auto& node_value : nodes_) {
        if (node_value.leaf && node_value.box.overlaps(box))
            result.push_back(node_value.key);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<LectNodeKey> LectTree::leaf_keys() const {
    std::vector<LectNodeKey> result;
    for (const auto& node_value : nodes_)
        if (node_value.leaf)
            result.push_back(node_value.key);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<LectNode> LectTree::all_nodes() const { return nodes_; }

Result<void> LectTree::save(const std::filesystem::path& directory) const {
    if (std::filesystem::exists(directory)) {
        return Result<void>::failure(StatusCode::IoError, "LECT destination already exists",
                                     directory.string());
    }
    const auto temporary = temporary_sibling(directory);
    std::error_code error;
    std::filesystem::create_directories(temporary, error);
    if (error)
        return Result<void>::failure(StatusCode::IoError, "failed to create LECT temporary directory",
                                     temporary.string());
    const auto payload_path = temporary / "nodes.bin";
    auto payload = internal::encode_lect_tree(*this);
    auto write = payload.save(payload_path);
    if (!write) {
        std::filesystem::remove_all(temporary, error);
        return write;
    }
    const auto checksum = internal::sha256(std::span<const std::byte>(payload.data()));
    internal::Json manifest(internal::Json::Object{
        {"dimension", static_cast<double>(root_.dimension())},
        {"format", "rbfsafe-lect"},
        {"nodes", static_cast<double>(size())},
        {"nodes_sha256", checksum},
        {"schema", static_cast<double>(kLectSchemaVersion)},
    });
    auto manifest_write = internal::write_text_file(temporary / "manifest.json", manifest.dump(true) + "\n");
    if (!manifest_write) {
        std::filesystem::remove_all(temporary, error);
        return manifest_write;
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        std::filesystem::remove_all(temporary, error);
        return Result<void>::failure(StatusCode::IoError, "failed to publish LECT directory",
                                     directory.string());
    }
    return Result<void>::success();
}

LectSnapshot LectSnapshot::from_tree(LectTree tree) {
    return LectSnapshot(std::make_shared<const LectTree>(std::move(tree)));
}

Result<LectSnapshot> LectSnapshot::open(const std::filesystem::path& directory) {
    auto manifest = internal::read_json_file(directory / "manifest.json");
    if (!manifest)
        return manifest.error();
    if (!manifest.value().is_object())
        return Result<LectSnapshot>::failure(StatusCode::CorruptData, "LECT manifest must be an object");
    const auto* format = manifest.value().find("format");
    const auto* schema = manifest.value().find("schema");
    const auto* checksum = manifest.value().find("nodes_sha256");
    if (format == nullptr || !format->is_string() || format->as_string() != "rbfsafe-lect" ||
        schema == nullptr || !schema->is_number() || schema->as_number() != kLectSchemaVersion ||
        checksum == nullptr || !checksum->is_string()) {
        return Result<LectSnapshot>::failure(StatusCode::IncompatibleFormat, "invalid LECT manifest");
    }
    const auto payload_path = directory / "nodes.bin";
    auto payload = internal::read_binary_file(payload_path);
    if (!payload)
        return payload.error();
    if (internal::sha256(std::span<const std::byte>(payload.value())) != checksum->as_string()) {
        return Result<LectSnapshot>::failure(StatusCode::CorruptData, "LECT checksum mismatch",
                                             payload_path.string());
    }
    auto tree = internal::decode_lect_tree(payload.value());
    if (!tree)
        return tree.error();
    return from_tree(std::move(tree).value());
}

const CspaceAabb& LectSnapshot::root_domain() const {
    static const CspaceAabb empty;
    return tree_ ? tree_->root_domain() : empty;
}
const SplitPolicy& LectSnapshot::policy() const {
    static const SplitPolicy empty;
    return tree_ ? tree_->policy() : empty;
}
std::size_t LectSnapshot::size() const noexcept { return tree_ ? tree_->size() : 0; }
Result<LectNode> LectSnapshot::node(const LectNodeKey& key) const {
    if (!tree_)
        return Result<LectNode>::failure(StatusCode::InvalidArgument, "LECT snapshot is empty");
    return tree_->node(key);
}
Result<LectNodeKey> LectSnapshot::locate(std::span<const double> configuration) const {
    if (!tree_)
        return Result<LectNodeKey>::failure(StatusCode::InvalidArgument, "LECT snapshot is empty");
    return tree_->locate(configuration);
}
Result<std::vector<LectNodeKey>> LectSnapshot::overlap_leaves(const CspaceAabb& box) const {
    if (!tree_)
        return Result<std::vector<LectNodeKey>>::failure(StatusCode::InvalidArgument,
                                                         "LECT snapshot is empty");
    return tree_->overlap_leaves(box);
}
std::vector<LectNodeKey> LectSnapshot::leaf_keys() const {
    return tree_ ? tree_->leaf_keys() : std::vector<LectNodeKey>{};
}
std::vector<LectNode> LectSnapshot::all_nodes() const {
    return tree_ ? tree_->all_nodes() : std::vector<LectNode>{};
}

} // namespace rbfsafe
