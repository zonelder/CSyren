#pragma once
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace csyren::cstdmf::tree
{
    // Trees are supplied through projections; the algorithms own no nodes.
    // Visitors must not change child lists during traversal. Collect IDs first
    // when removing a branch. Traversal is iterative, including for deep trees.
    template<class Key, class Children, class Visit>
    void preorder(Key root, Children children, Visit visit)
    {
        std::vector<Key> pending{root};
        while (!pending.empty()) {
            auto node = pending.back(); pending.pop_back();
            visit(node);
            const auto& branch = std::invoke(children, node);
            pending.insert(pending.end(), branch.rbegin(), branch.rend());
        }
    }

    template<class Key, class Children, class Visit>
    void postorder(Key root, Children children, Visit visit)
    {
        std::vector<std::pair<Key,bool>> pending{{root,false}};
        while (!pending.empty()) {
            auto [node, expanded] = pending.back(); pending.pop_back();
            if (expanded) { visit(node); continue; }
            pending.emplace_back(node,true);
            const auto& branch = std::invoke(children,node);
            for (auto it = branch.rbegin(); it != branch.rend(); ++it) pending.emplace_back(*it,false);
        }
    }

    template<class Key, class Parent>
    bool isAncestor(Key ancestor, Key node, Key end, Parent parent)
    {
        for (; node != end; node = std::invoke(parent, node))
            if (node == ancestor) return true;
        return false;
    }

    // Lookup returns a node pointer or nullptr. Parent/children are projections
    // returning mutable references (member pointers also work). Local node data
    // is preserved. A failed validation leaves both sides unchanged.
    template<class Key, class Lookup, class Parent, class Children>
    bool reparent(Key child, Key destination, Key end, Lookup lookup, Parent parent, Children children)
    {
        auto* node = lookup(child); auto* target = lookup(destination);
        if (!node || !target || isAncestor(child, destination, end, [&](Key id) {
            auto* p = lookup(id); return p ? std::invoke(parent, *p) : end;
        })) return false;
        auto& previous = std::invoke(parent, *node);
        if (previous == destination) return true;
        // Allocate before unlinking, so an allocation failure preserves the tree.
        std::invoke(children, *target).push_back(child);
        if (auto* old = lookup(previous)) std::erase(std::invoke(children, *old), child);
        previous = destination;
        return true;
    }
}
