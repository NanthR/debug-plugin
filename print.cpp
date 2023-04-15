#include <iostream>

#include "config.h"
#include "gcc-plugin.h"
#include "plugin-version.h"
#include "system.h"

#include "tree.h"

#include "c-family/c-common.h"
#include "c-family/c-pragma.h"
#include "diagnostic.h"
#include "stringpool.h"

#include "tree-iterator.h"

#include "print-tree.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <vector>

std::unordered_set<std::string> to_change_functions;

int plugin_is_GPL_compatible = 1;

static plugin_info decorator_info{.version = "1", .help = "Log function info"};

bool get_func_decl(location_t here, tree *func_decl, const char *name) {
  if (*func_decl != NULL_TREE) {
    return false;
  }

  tree ident = get_identifier(name);
  *func_decl = lookup_name(ident);
  if (*func_decl == NULL_TREE) {
    error_at(here,
             "plugin requires declaration of %s, please create the function",
             name);
    return false;
  }
  return true;
}

tree build_function_print_function_name(std::string name) {
  tree printf_decl = NULL_TREE;
  get_func_decl(0, &printf_decl, "printf");

  std::ostringstream out;
  out << "\033[1;32m";
  out << "Calling function: " << name;
  out << "\033[0m\n";

  std::string formatted_string = out.str();

  auto length = formatted_string.length();
  auto string = build_string_literal(length, formatted_string.c_str());
  auto call = build_call_expr(printf_decl, 1, string);

  return call;
}

void iterate_function_body(const char *format_string, tree *expr) {
  tree *body;
  if (TREE_CODE(*expr) == BIND_EXPR) {
    body = &BIND_EXPR_BODY(*expr);
  } else {
    body = expr;
  }

  if (TREE_CODE(*body) == STATEMENT_LIST) {
    for (tree_stmt_iterator i = tsi_start(*body); !tsi_end_p(i); tsi_next(&i)) {
      tree stmt = tsi_stmt(i);
      auto tree_code = TREE_CODE(stmt);

      if (tree_code == BIND_EXPR) {
        iterate_function_body(format_string, &stmt);
      } else if (tree_code == RETURN_EXPR) {
        std::ostringstream out;
        out << "\033[1;32m";
        out << "Result is " << format_string;
        out << "\033[0m\n";
        tree printf_decl = NULL_TREE;
        get_func_decl(0, &printf_decl, "printf");
        std::string formatted_string = out.str();
        auto length = formatted_string.length();
        auto string = build_string_literal(length, formatted_string.c_str());
        auto call = build_call_expr(printf_decl, 2, string,
                                    TREE_OPERAND(TREE_OPERAND(stmt, 0), 1));
        tsi_link_before(&i, call, TSI_NEW_STMT);
        return;
      } else if (tree_code == COND_EXPR) {
        int cur = 1;
        while (cur < TREE_OPERAND_LENGTH(stmt)) {
          auto branch = TREE_OPERAND(stmt, cur);
          iterate_function_body(format_string, &branch);
          cur++;
        }
      }
    }
  } else if (TREE_CODE(*body) == BIND_EXPR) {
    iterate_function_body(format_string, body);
  }
}

void add_function_return(tree function_decl, tree *function_body) {
  auto return_decl = DECL_RESULT(function_decl);
  auto return_type = TREE_TYPE(return_decl);
  if (VOID_TYPE_P(return_type)) {
    auto return_string = "\033[1;32mFunction returns void type\033[0m\n";
    tree printf_decl = NULL_TREE;
    get_func_decl(0, &printf_decl, "printf");

    auto length = strlen(return_string);
    auto string = build_string_literal(length, return_string);
    auto call = build_call_expr(printf_decl, 1, string);

    if (TREE_CODE(*function_body) == STATEMENT_LIST) {
      auto i = tsi_last(*function_body);
      tsi_link_after(&i, call, TSI_NEW_STMT);
    } else {
      auto new_stmt_list = alloc_stmt_list();
      auto i = tsi_start(new_stmt_list);
      tsi_link_after(&i, *function_body, TSI_NEW_STMT);
      tsi_link_after(&i, call, TSI_NEW_STMT);
      *function_body = new_stmt_list;
    }
  } else if (return_type == char_type_node) {
    iterate_function_body("%c", function_body);
  } else if (return_type == integer_type_node) {
    iterate_function_body("%d", function_body);
  } else if (REAL_TYPE_CHECK(return_decl)) {
    iterate_function_body("%f", function_body);
  }
}

std::vector<tree> build_function_print_all_params(tree function_decl) {
  std::vector<tree> res;
  auto arg = DECL_ARGUMENTS(function_decl);
  tree printf_decl = NULL_TREE;
  get_func_decl(0, &printf_decl, "printf");
  while (arg != NULL_TREE) {
    std::string arg_name = IDENTIFIER_POINTER(DECL_NAME(arg));
    std::ostringstream out;
    out << "\033[1;32m";
    out << "Param: " << arg_name;
    if (TREE_TYPE(arg) == char_type_node)
      out << "; Value: %c";
    else if (TREE_CODE(TREE_TYPE(arg)) == POINTER_TYPE) {
      auto type_name =
          IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(TREE_TYPE(TREE_TYPE(arg)))));
      if (strcmp(type_name, "char") == 0)
        out << "; Value: %s";
    } else if (TREE_TYPE(arg) == integer_type_node)
      out << "; Value: %d";
    else if (REAL_TYPE_CHECK(arg))
      out << "; Value: %f";

    out << "\033[0m\n";
    std::string formatted_string = out.str();
    auto length = formatted_string.length();
    auto string = build_string_literal(length, formatted_string.c_str());
    auto call = build_call_expr(printf_decl, 2, string, arg);
    res.push_back(call);
    arg = DECL_CHAIN(arg);
  }
  return res;
}

tree add_print_to_function(tree function_decl, tree function_body,
                           std::string name) {
  tree body;
  if (TREE_CODE(function_body) == BIND_EXPR) {
    body = BIND_EXPR_BODY(function_body);
  } else {
    body = function_body;
  }

  auto res = NULL_TREE;

  auto call = build_function_print_function_name(name);
  auto func_params = build_function_print_all_params(function_decl);
  if (TREE_CODE(body) == STATEMENT_LIST) {
    auto i = tsi_start(body);
    tsi_link_before(&i, call, TSI_NEW_STMT);
    for (auto t : func_params) {
      tsi_link_after(&i, t, TSI_NEW_STMT);
    }
    res = body;
  } else {
    auto new_stmt_list = alloc_stmt_list();
    auto i = tsi_start(new_stmt_list);
    tsi_link_after(&i, call, TSI_NEW_STMT);
    for (auto t : func_params) {
      tsi_link_after(&i, t, TSI_NEW_STMT);
    }
    tsi_link_after(&i, body, TSI_NEW_STMT);
    res = new_stmt_list;
  }
  add_function_return(function_decl, &res);
  return res;
}

static void add_print(void *event_data, void *user_data) {
  tree t = (tree)event_data;
  if (t == NULL_TREE) {
    return;
  }
  if (TREE_CODE(t) == FUNCTION_DECL) {
    auto name = IDENTIFIER_POINTER(DECL_NAME(t));
    if (to_change_functions.count(name) == 1) {
      DECL_SAVED_TREE(t) = add_print_to_function(t, DECL_SAVED_TREE(t), name);
    }
  }
}

static void handle_pragma_decorator(cpp_reader *reader) {
  if (!cfun) {
    warning(OPT_Wpragmas, "Cannot use pragma outside a function");
    return;
  }
  to_change_functions.insert(IDENTIFIER_POINTER(DECL_NAME(cfun->decl)));
}

static void register_my_pragma(void *event_data, void *user_data) {
  c_register_pragma("GCCPLUGIN", "debug", handle_pragma_decorator);
}

int plugin_init(plugin_name_args *plugin_info, plugin_gcc_version *version) {
  if (!plugin_default_version_check(version, &gcc_version)) {
    std::cout << "This GCC plugin is for version " << GCCPLUGIN_VERSION_MAJOR
              << "." << GCCPLUGIN_VERSION_MINOR << "\n\n";
    return 1;
  }

  register_callback(plugin_info->base_name, PLUGIN_PRAGMAS, register_my_pragma,
                    &decorator_info);
  register_callback(plugin_info->base_name, PLUGIN_PRE_GENERICIZE, add_print,
                    NULL);

  return 0;
}
