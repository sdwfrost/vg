#ifndef VG_GBWT_HELPER_HPP_INCLUDED
#define VG_GBWT_HELPER_HPP_INCLUDED

/** \file 
 * Utility classes and functions for working with GBWT.
 */

#include <vector>

#include "handle.hpp"
#include "position.hpp"
#include "xg.hpp"

#include <gbwt/dynamic_gbwt.h>
#include <sdsl/int_vector.hpp>

namespace vg {

//------------------------------------------------------------------------------

/// Convert gbwt::node_type to handle_t.
inline handle_t gbwt_to_handle(const HandleGraph& graph, gbwt::node_type node) {
    return graph.get_handle(gbwt::Node::id(node), gbwt::Node::is_reverse(node));
}

/// Convert gbwt::node_type and an offset as size_t to pos_t.
inline pos_t gbwt_to_pos(gbwt::node_type node, size_t offset) {
    return make_pos_t(gbwt::Node::id(node), gbwt::Node::is_reverse(node), offset);
}

/// Convert gbwt::node_type to xg::XG:ThreadMapping.
inline xg::XG::ThreadMapping gbwt_to_thread_mapping(gbwt::node_type node) {
    return { static_cast<int64_t>(gbwt::Node::id(node)), gbwt::Node::is_reverse(node) };
}

/// Convert handle_t to gbwt::node_type.
inline gbwt::node_type handle_to_gbwt(const HandleGraph& graph, handle_t handle) {
    return gbwt::Node::encode(graph.get_id(handle), graph.get_is_reverse(handle));
}

/// Extract gbwt::node_type from pos_t.
inline gbwt::node_type pos_to_gbwt(pos_t pos) {
    return gbwt::Node::encode(id(pos), is_rev(pos));
}

/// Convert Mapping to gbwt::node_type.
inline gbwt::node_type mapping_to_gbwt(const Mapping& mapping) {
    return gbwt::Node::encode(mapping.position().node_id(), mapping.position().is_reverse());
}

/// Convert a node on xg::XGPath to gbwt::node_type.
inline gbwt::node_type xg_path_to_gbwt(const xg::XGPath& path, size_t i) {
    return gbwt::Node::encode(path.node(i), path.is_reverse(i));
}

/// Get a string representation of a thread name stored in GBWT metadata.
std::string thread_name(const gbwt::GBWT& gbwt_index, size_t i);

//------------------------------------------------------------------------------

/**
 * A runtime-only HandleGraph implementation that uses GBWT for graph topology and
 * extracts sequences from another HandleGraph. Faster sequence access but slower
 * graph navigation than in XG. Also supports a version of follow_edges() that
 * takes only paths supported by the indexed haplotypes.
 */
class GBWTGraph : public HandleGraph {
public:
    /// Create a graph backed by the GBWT index and extract the sequences from the
    /// given HandleGraph.
    GBWTGraph(const gbwt::GBWT& gbwt_index, const HandleGraph& sequence_source);

    /// Copy constructor.
    GBWTGraph(const GBWTGraph& source);

    /// Move constructor.
    GBWTGraph(GBWTGraph&& source);

    const gbwt::GBWT&   index;
    std::vector<char>   sequences;
    sdsl::int_vector<0> offsets;
    std::vector<bool>   real_nodes;
    size_t              total_nodes;

    constexpr static size_t CHUNK_SIZE = 1024; // For parallel for_each_handle().

//------------------------------------------------------------------------------

public:
    // Standard HandleGraph interface.

    /// Method to check if a node exists by ID.
    virtual bool has_node(id_t node_id) const;

    /// Look up the handle for the node with the given ID in the given orientation.
    virtual handle_t get_handle(const id_t& node_id, bool is_reverse = false) const;

    /// Get the ID from a handle.
    virtual id_t get_id(const handle_t& handle) const;

    /// Get the orientation of a handle.
    virtual bool get_is_reverse(const handle_t& handle) const;

    /// Invert the orientation of a handle (potentially without getting its ID).
    virtual handle_t flip(const handle_t& handle) const;

    /// Get the length of a node.
    virtual size_t get_length(const handle_t& handle) const;

    /// Get the sequence of a node, presented in the handle's local forward
    /// orientation.
    virtual std::string get_sequence(const handle_t& handle) const;

    /// Return the number of nodes in the graph.
    virtual size_t get_node_count() const;

    /// Return the smallest ID in the graph, or some smaller number if the
    /// smallest ID is unavailable. Return value is unspecified if the graph is empty.
    virtual id_t min_node_id() const;

    /// Return the largest ID in the graph, or some larger number if the
    /// largest ID is unavailable. Return value is unspecified if the graph is empty.
    virtual id_t max_node_id() const;

protected:

    /// Loop over all the handles to next/previous (right/left) nodes. Passes
    /// them to a callback which returns false to stop iterating and true to
    /// continue. Returns true if we finished and false if we stopped early.
    virtual bool follow_edges_impl(const handle_t& handle, bool go_left, const std::function<bool(const handle_t&)>& iteratee) const;

    /// Loop over all the nodes in the graph in their local forward
    /// orientations, in their internal stored order. Stop if the iteratee
    /// returns false. Can be told to run in parallel, in which case stopping
    /// after a false return value is on a best-effort basis and iteration
    /// order is not defined. Returns true if we finished and false if we 
    /// stopped early.
    virtual bool for_each_handle_impl(const std::function<bool(const handle_t&)>& iteratee, bool parallel = false) const;

//------------------------------------------------------------------------------

public:

    // GBWTGraph specific interface.

    /// Convert gbwt::node_type to handle_t.
    static handle_t node_to_handle(gbwt::node_type node) { return handlegraph::as_handle(node); }

    /// Convert handle_t to gbwt::node_type.
    static gbwt::node_type handle_to_node(const handle_t& handle) { return handlegraph::as_integer(handle); }

    /// Get node sequence as a pointer and length.
    std::pair<const char*, size_t> get_sequence_view(const handle_t& handle) const;

    /// Determine if the node sequence starts with the given character.
    bool starts_with(const handle_t& handle, char c) const;

    /// Determine if the node sequence ends with the given character.
    bool ends_with(const handle_t& handle, char c) const;

    /// Convert handle_t to gbwt::SearchState.
    gbwt::SearchState get_state(const handle_t& handle) const { return this->index.find(handle_to_node(handle)); }

    /// Convert handle_t to gbwt::BidirectionalState.
    gbwt::BidirectionalState get_bd_state(const handle_t& handle) const { return this->index.bdFind(handle_to_node(handle)); }

    /// Visit all successor states of this state and call iteratee for the state.
    /// Stop and return false if the iteratee returns false.
    /// Note that the state may be empty if no path continues to that node.
    bool follow_paths(gbwt::SearchState state, const std::function<bool(const gbwt::SearchState&)>& iteratee) const;

    /// Visit all predecessor/successor states of this state and call iteratee for the state.
    /// Stop and return false if the iteratee returns false.
    /// Note that the state may be empty if no path continues to that node.
    /// Each state corresponds to a path. Going backward extends the path left, while going
    /// extends it right. When going backward, the state is for the reverse orientation.
    bool follow_paths(gbwt::BidirectionalState state, bool backward,
                      const std::function<bool(const gbwt::BidirectionalState&)>& iteratee) const;

private:
    size_t node_offset(gbwt::node_type node) const { return node - this->index.firstNode(); }
    size_t node_offset(const handle_t& handle) const { return this->node_offset(handle_to_node(handle)); }
};

//------------------------------------------------------------------------------

/// Traverse all haplotype-consistent windows in the graph and call lambda() for each window.
/// Uses multiple threads, so the lambda should be thread-safe.
/// A window starts with the sequence of a node and is followed by window_size - 1 bases
/// from subsequent nodes. If no extensions are possible, a shorter substring of
/// length >= window_size also qualifies as a window.
void for_each_haplotype_window(const GBWTGraph& graph, size_t window_size,
                               const std::function<void(const std::vector<handle_t>&, const std::string&)>& lambda,
                               bool parallel);

/// Iterate over all windows in the graph, running lambda on each.
void for_each_window(const HandleGraph& graph, size_t window_size,
                     const std::function<void(const std::vector<handle_t>&, const std::string&)>& lambda,
                     bool parallel);

//------------------------------------------------------------------------------

/// Transform the paths into a GBWT index. Primarily for testing.
gbwt::GBWT get_gbwt(const std::vector<gbwt::vector_type>& paths);

//------------------------------------------------------------------------------

} // namespace vg

#endif // VG_GBWT_HELPER_HPP_INCLUDED
