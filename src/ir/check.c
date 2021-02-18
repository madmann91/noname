#include "ir/node.h"

node_t check_and_convert_node(mod_t mod, node_t node) {
    switch (node->tag) {
        case NODE_UNI:
        case NODE_NAT:
        case NODE_INT:
        case NODE_FLOAT:
        case NODE_STAR:
            return import_node(mod, node);
        default:
            assert(false && "invalid node tag");
            break;
    }
}
