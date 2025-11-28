/**
 * SUB Language Middle-End Implementation
 */

#include "semantic_analyzer.hpp"
#include <iostream>
#include <algorithm>

namespace sub {
namespace middle_end {

DataType SemanticAnalyzer::infer_type(const ASTNode* node) {
    if (!node) return DataType::Unknown;
    
    switch (node->type) {
        case ASTNodeType::Literal:
            if (node->value.find('.') != std::string::npos)
                return DataType::Float;
            if (node->value == "true" || node->value == "false")
                return DataType::Bool;
            if (node->value.front() == '"' || node->value.front() == '\'')
                return DataType::String;
            return DataType::Int;
        
        case ASTNodeType::Identifier: {
            auto symbol = symbol_table_.lookup(node->value);
            return symbol ? symbol->type : DataType::Unknown;
        }
        
        case ASTNodeType::BinaryExpr:
            return infer_type(node->children[0].get());
        
        default:
            return DataType::Unknown;
    }
}

bool SemanticAnalyzer::check_type_compatibility(DataType expected, DataType actual) {
    if (expected == DataType::Auto || actual == DataType::Auto)
        return true;
    if (expected == actual)
        return true;
    if ((expected == DataType::Float && actual == DataType::Int) ||
        (expected == DataType::Int && actual == DataType::Float))
        return true;
    return false;
}

void SemanticAnalyzer::analyze_node(ASTNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case ASTNodeType::VarDecl:
            analyze_var_decl(node);
            break;
        case ASTNodeType::FunctionDecl:
            analyze_function_decl(node);
            break;
        case ASTNodeType::BinaryExpr:
            analyze_binary_expr(node);
            break;
        case ASTNodeType::CallExpr:
            analyze_call_expr(node);
            break;
        case ASTNodeType::Block:
            symbol_table_.enter_scope();
            for (auto& child : node->children) {
                analyze_node(child.get());
            }
            symbol_table_.exit_scope();
            break;
        default:
            for (auto& child : node->children) {
                analyze_node(child.get());
            }
    }
}

void SemanticAnalyzer::analyze_var_decl(ASTNode* node) {
    const std::string& var_name = node->value;
    bool is_const = node->attributes.count("const") > 0;
    
    DataType declared_type = DataType::Auto;
    if (node->attributes.count("type")) {
        // Parse type annotation
    }
    
    if (node->children.size() > 0) {
        DataType init_type = infer_type(node->children[0].get());
        if (declared_type == DataType::Auto) {
            declared_type = init_type;
        } else if (!check_type_compatibility(declared_type, init_type)) {
            report_error("Type mismatch in variable declaration", 
                        node->line, node->column);
        }
    }
    
    if (!symbol_table_.insert(var_name, declared_type, is_const, node->line)) {
        report_error("Variable '" + var_name + "' already declared in this scope",
                    node->line, node->column);
    }
}

void SemanticAnalyzer::analyze_function_decl(ASTNode* node) {
    symbol_table_.enter_scope();
    // Analyze function body
    for (auto& child : node->children) {
        analyze_node(child.get());
    }
    symbol_table_.exit_scope();
}

void SemanticAnalyzer::analyze_binary_expr(ASTNode* node) {
    if (node->children.size() < 2) return;
    
    DataType left_type = infer_type(node->children[0].get());
    DataType right_type = infer_type(node->children[1].get());
    
    if (!check_type_compatibility(left_type, right_type)) {
        report_warning("Potential type mismatch in binary expression",
                      node->line, node->column);
    }
}

void SemanticAnalyzer::analyze_call_expr(ASTNode* node) {
    auto symbol = symbol_table_.lookup(node->value);
    if (!symbol) {
        report_error("Undefined function '" + node->value + "'",
                    node->line, node->column);
    }
}

void ConstantFoldingPass::run(ASTNode* root, int& level) {
    if (!root) return;
    
    if (root->type == ASTNodeType::BinaryExpr && root->children.size() == 2) {
        auto* left = root->children[0].get();
        auto* right = root->children[1].get();
        
        if (left->type == ASTNodeType::Literal && 
            right->type == ASTNodeType::Literal) {
            // Fold constants
            try {
                int left_val = std::stoi(left->value);
                int right_val = std::stoi(right->value);
                int result = 0;
                
                if (root->value == "+") result = left_val + right_val;
                else if (root->value == "-") result = left_val - right_val;
                else if (root->value == "*") result = left_val * right_val;
                else if (root->value == "/" && right_val != 0) result = left_val / right_val;
                
                root->type = ASTNodeType::Literal;
                root->value = std::to_string(result);
                root->children.clear();
            } catch (...) {
                // Not integer literals, skip
            }
        }
    }
    
    for (auto& child : root->children) {
        run(child.get(), level);
    }
}

void DeadCodeEliminationPass::run(ASTNode* root, int& level) {
    if (!root) return;
    
    // Remove unreachable code after return statements
    auto& children = root->children;
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i]->type == ASTNodeType::Literal &&
            children[i]->value == "return") {
            children.erase(children.begin() + i + 1, children.end());
            break;
        }
    }
    
    for (auto& child : root->children) {
        run(child.get(), level);
    }
}

void InlineExpansionPass::run(ASTNode* root, int& level) {
    // Inline small functions (simplified)
    for (auto& child : root->children) {
        run(child.get(), level);
    }
}

} // namespace middle_end
} // namespace sub

extern "C" {

void* sub_middle_end_create_analyzer() {
    return new sub::middle_end::SemanticAnalyzer();
}

int sub_middle_end_analyze(void* analyzer, void* ast_root) {
    auto* sem = static_cast<sub::middle_end::SemanticAnalyzer*>(analyzer);
    auto* root = static_cast<sub::middle_end::ASTNode*>(ast_root);
    return sem->analyze(root) ? 1 : 0;
}

void sub_middle_end_optimize(void* ast_root, int level) {
    auto* root = static_cast<sub::middle_end::ASTNode*>(ast_root);
    sub::middle_end::Optimizer optimizer(level);
    optimizer.optimize(root);
}

void sub_middle_end_free_analyzer(void* analyzer) {
    delete static_cast<sub::middle_end::SemanticAnalyzer*>(analyzer);
}

const char* sub_middle_end_get_errors(void* analyzer) {
    auto* sem = static_cast<sub::middle_end::SemanticAnalyzer*>(analyzer);
    static std::string errors;
    errors.clear();
    for (const auto& err : sem->get_errors()) {
        errors += err + "\n";
    }
    return errors.c_str();
}

}
