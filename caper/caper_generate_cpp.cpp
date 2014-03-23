// Copyright (C) 2008 Naoyuki Hirayama.
// All Rights Reserved.

// $Id$

#include "caper_ast.hpp"
#include "caper_generate_cpp.hpp"
#include "caper_format.hpp"
#include "caper_stencil.hpp"
#include "caper_finder.hpp"
#include <algorithm>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

namespace {

std::string make_type_name(const Type& x) {
    switch(x.extension) {
        case Extension::None:
            return x.name;
        case Extension::Star:
        case Extension::Plus:
            return "Sequence<" + x.name + ">";
        case Extension::Question:
            return "Option<" + x.name + ">";
        default:
            assert(0);
            return "";
    }
}
        
std::string make_arg_decl(const Type& x, size_t l) {
    std::string sl = std::to_string(l);
    std::string y = make_type_name(x) + " arg" + sl;
    switch (x.extension) {
        case Extension::None:
            return y;
        case Extension::Star:
        case Extension::Plus:
            return
                y + "(sa_, stack_, seq_get_range(base, arg_index" + sl + "))";
        case Extension::Question:
            return y + "()";
        default:
            assert(0);
            return "";
    }
}

void make_signature(
    const std::map<std::string, Type>&      nonterminal_types,
    const tgt::parsing_table::rule_type&    rule,
    const SemanticAction&                   sa,
    std::vector<std::string>&               signature) {
    // function name
    signature.push_back(sa.name);

    // return value
    signature.push_back(
        make_type_name(*finder(nonterminal_types, rule.left().name())));

    // arguments
    for (const auto& arg: sa.args) {
        signature.push_back(make_type_name(arg.type));
    }
}

} // unnamed namespace

void generate_cpp(
    const std::string&                  src_filename,
    std::ostream&                       os,
    const GenerateOptions&              options,
    const std::map<std::string, Type>&  terminal_types,
    const std::map<std::string, Type>&  nonterminal_types,
    const std::vector<std::string>&     tokens,
    const action_map_type&              actions,
    const tgt::parsing_table&           table) {

    const char* ind1 = "    ";

#ifdef _WINDOWS
    char basename[_MAX_PATH];
    char extension[_MAX_PATH];
    _splitpath(src_filename.c_str(), NULL, NULL, basename, extension);
    std::string filename = std::string(basename)+ extension;
# else
    std::string filename = src_filename;
#endif

    std::string headername = filename;
    for (auto& x: headername){
        if (!isalpha(x) && !isdigit(x)) {
            x = '_';
        } else {
            x = toupper(x);
        }
    }

    // once header / notice / URL / includes / namespace header
    stencil(
        os, R"(
#ifndef ${headername}_
#define ${headername}_

// This file was automatically generated by Caper.
// (http://jonigata.github.io/caper/caper.html)

#include <cstdlib>
#include <cassert>
$${debug_include}
$${use_stl}

namespace ${namespace_name} {

)",
        
        {"headername", headername},
        {"debug_include",
            {options.debug_parser ? "#include <iostream>\n" : ""}},
        {"use_stl",
            {options.dont_use_stl ? "" : "#include <vector>\n"}},
        {"namespace_name", options.namespace_name}
        );

    if (!options.external_token) {
        // token enumeration
        stencil(
            os, R"(
enum Token {
$${tokens}
};

inline const char* token_label(Token t) {
    static const char* labels[] = {
$${labels}
    };
    return labels[t];
}

)",
            {"tokens", [&](std::ostream& os){
                    for(const auto& token: tokens) {
                        stencil(
                            os, R"(
    ${prefix}${token},
)",
                            {"prefix", options.token_prefix},
                            {"token", token}
                            );
                    }
                }},
            {"labels", [&](std::ostream& os){
                    for(const auto& token: tokens) {
                        stencil(
                            os, R"(
        "${prefix}${token}",
)",
                            {"prefix", options.token_prefix},
                            {"token", token}
                            );
                    }
                }}
            );

    }

    // stack class header
    if (!options.dont_use_stl) {
        // STL version
        stencil(
            os, R"(
template <class T, unsigned int StackSize>
class Stack {
public:
    Stack() { gap_ = 0; }

    void rollback_tmp() {
        gap_ = stack_.size();
        tmp_.clear();
    }

    void commit_tmp() {
        // may throw
        stack_.reserve(gap_ + tmp_.size());
	   
        // expect not to throw
        stack_.erase(stack_.begin()+ gap_, stack_.end());
        stack_.insert(stack_.end(), tmp_.begin(), tmp_.end());
    }
    bool push(const T& f) {
        if (StackSize != 0 && StackSize <= stack_.size()+ tmp_.size()) {
            return false;
        }
        tmp_.push_back(f);
        return true;
    }
	   
    void pop(size_t n) {
        if (tmp_.size() < n) {
            n -= tmp_.size();
            tmp_.clear();
            gap_ -= n;
        } else {
            tmp_.erase(tmp_.end() - n, tmp_.end());
        }
    }

    T& top() {
        assert(0 < depth());
        if (!tmp_.empty()) {
            return tmp_.back();
        } else {
            return stack_[gap_ - 1];
        }
    }
	   
    const T& get_arg(size_t base, size_t index) {
        size_t n = tmp_.size();
        if (base - index <= n) {
            return tmp_[n - (base - index)];
        } else {
            return stack_[gap_ - (base - n) + index];
        }
    }
	   
    void clear() {
        stack_.clear();
    }
	   
    bool empty() const {
        if (!tmp_.empty()) {
            return false;
        } else {
            return gap_ == 0;
        }
    }
	   
    size_t depth() const {
        return gap_ + tmp_.size();
    }
	   
    T& nth(size_t index) {
        if (gap_ <= index) {
            return tmp_[index - gap_];
        } else {
            return stack_[index];
        }
    }

    void swap_top_and_second() {
        int d = depth();
        assert(2 <= d);
        T x = nth(d - 1);
        nth(d - 1) = nth(d - 2);
        nth(d - 2) = x;
    }

private:
    std::vector<T> stack_;
    std::vector<T> tmp_;
    size_t gap_;
	   
};

)");
    } else {
        // bulkmemory version
        stencil(
            os, R"(
template <class T, unsigned int StackSize>
class Stack {
public:
    Stack() { top_ = 0; gap_ = 0; tmp_ = 0; }
    ~Stack() {}

    void rollback_tmp() {
        for (size_t i = 0 ; i <tmp_ ; i++) {
            at(StackSize - 1 - i).~T(); // explicit destructor
        }
        tmp_ = 0;
        gap_ = top_;
    }

    void commit_tmp() {
        for (size_t i = 0 ; i <tmp_ ; i++) {
            if (gap_ + i <top_) {
                at(gap_ + i) = at(StackSize - 1 - i);
            } else {
                new (&at(gap_ + i)) T(at(StackSize - 1 - i));
            }
            at(StackSize - 1 - i).~T(); // explicit destructor
        }
        if (gap_ + tmp_ <top_) {
            for (int i = 0 ; i <int(top_ - gap_ - tmp_); i++) {
                at(top_ - 1 - i).~T(); // explicit destructor
            }
        }

        top_ = gap_ = gap_ + tmp_;
        tmp_ = 0;
    }

    bool push(const T& f) {
        if (StackSize <= top_ + tmp_) { return false; }
        new (&at(StackSize - 1 - tmp_++)) T(f);
        return true;
    }

    void pop(size_t n) {
        size_t m = n; if (m > tmp_) { m = tmp_; }

        for (size_t i = 0 ; i <m ; i++) {
            at(StackSize - tmp_ + i).~T(); // explicit destructor
        }

        tmp_ -= m;
        gap_ -= n - m;
    }

    T& top() {
        assert(0 < depth());
        if (0 <tmp_) {
            return at(StackSize - 1 -(tmp_-1));
        } else {
            return at(gap_ - 1);
        }
    }

    const T& get_arg(size_t base, size_t index) {
        if (base - index <= tmp_) {
            return at(StackSize-1 - (tmp_ -(base - index)));
        } else {
            return at(gap_ -(base - tmp_) + index);
        }
    }

    void clear() {
        rollback_tmp();
        for (size_t i = 0 ; i <top_ ; i++) {
            at(i).~T(); // explicit destructor
        }
        top_ = gap_ = tmp_ = 0;
    }

    bool empty() const {
        if (0 < tmp_) {
            return false;
        } else {
            return gap_ == 0;
        }
    }

    size_t depth() const {
        return gap_ + tmp_;
    }

    T& nth(size_t index) {
        if (gap_ <= index) {
            return at(StackSize-1 - (index - gap_));
        } else {
            return at(index);
        }
    }

    void swap_top_and_second() {
        int d = depth();
        assert(2 <= d);
        T x = nth(d - 1);
        nth(d - 1) = nth(d - 2);
        nth(d - 2) = x;
    }

private:
    T& at(size_t n) {
        return *(T*)(stack_ + (n * sizeof(T)));
    }

private:
    char stack_[ StackSize * sizeof(T) ];
    size_t top_;
    size_t gap_;
    size_t tmp_;

};

)"
            );
    }

    // parser class header
    stencil(
        os, R"(
template <${token_parameter}class Value, class SemanticAction,
          unsigned int StackSize = ${default_stack_size}>
class Parser {
public:
    typedef Token token_type;
    typedef Value value_type;

    enum Nonterminal {
)",
        {"token_parameter", options.external_token ? "class Token, " : ""},
        {"default_stack_size", options.dont_use_stl ? "1024" : "0"}
        );

    for (const auto& nonterminal_type: nonterminal_types) {
        stencil(
            os, R"(
        Nonterminal_${nonterminal_name},
)",
            {"nonterminal_name", nonterminal_type.first}
            );
    }
    
    stencil(
        os, R"(
    };

public:
    Parser(SemanticAction& sa) : sa_(sa) { reset(); }

    void reset() {
        error_ = false;
        accepted_ = false;
        clear_stack();
        rollback_tmp_stack();
        if (push_stack(${first_state}, value_type())) {
            commit_tmp_stack();
        } else {
            sa_.stack_overflow();
            error_ = true;
        }
    }

    bool post(token_type token, const value_type& value) {
        rollback_tmp_stack();
        error_ = false;
        while ((this->*(stack_top()->entry->state))(token, value))
            ; // may throw
        if (!error_) {
            commit_tmp_stack();
        } else {
            recover(token, value);
        }
        return accepted_ || error_;
    }

    bool accept(value_type& v) {
        assert(accepted_);
        if (error_) { return false; }
        v = accepted_value_;
        return true;
    }

    bool error() { return error_; }

)",
        {"first_state", table.first_state()},
        {"first_state_handle_error",
                table.states()[table.first_state()].handle_error}
        );

    // implementation
    stencil(
        os, R"(
private:
    typedef Parser<${token_paremter}Value, SemanticAction, StackSize> self_type;

    typedef bool (self_type::*state_type)(token_type, const value_type&);
    typedef int (self_type::*gotof_type)(Nonterminal);

    bool            accepted_;
    bool            error_;
    value_type      accepted_value_;
    SemanticAction& sa_;

    struct table_entry {
        state_type  state;
        gotof_type  gotof;
        bool        handle_error;
    };

    struct stack_frame {
        const table_entry*  entry;
        value_type          value;
        int                 sequence_length;

        stack_frame(const table_entry* e, const value_type& v, int sl)
            : entry(e), value(v), sequence_length(sl) {}
    };

)",
        {"token_paremter", options.external_token ? "Token, " : ""}
        );

    // stack operation
    stencil(
        os, R"(
    Stack<stack_frame, StackSize> stack_;

    bool push_stack(int state_index, const value_type& v, int sl = 0) {
        bool f = stack_.push(stack_frame(entry(state_index), v, sl));
        assert(!error_);
        if (!f) { 
            error_ = true;
            sa_.stack_overflow();
        }
        return f;
    }

    void pop_stack(size_t n) {
$${pop_stack_implementation}
    }

    stack_frame* stack_top() {
        return &stack_.top();
    }

    const value_type& get_arg(size_t base, size_t index) {
        return stack_.get_arg(base, index).value;
    }

    void clear_stack() {
        stack_.clear();
    }

    void rollback_tmp_stack() {
        stack_.rollback_tmp();
    }

    void commit_tmp_stack() {
        stack_.commit_tmp();
    }

)",
        {"pop_stack_implementation", [&](std::ostream& os) {
                if (options.allow_ebnf) {
                    stencil(
                        os, R"(
        int nn = int(n);
        while(nn--) {
            stack_.pop(1 + stack_.top().sequence_length);
        }
)"
                        );
                } else {
                    stencil(
                        os, R"(
        stack_.pop( n );
)"
                        );
                }
            }}
        );

    if (options.recovery) {
        stencil(
            os, R"(
    void recover(Token token, const value_type& value) {
        rollback_tmp_stack();
        error_ = false;
$${debmes:start}
        while(!stack_top()->entry->handle_error) {
            pop_stack(1);
            if (stack_.empty()) {
$${debmes:failed}
                error_ = true;
                return;
            }
        }
$${debmes:done}
        // post error_token;
$${debmes:post_error_start}
        while ((this->*(stack_top()->entry->state))(${recovery_token}, value_type()));
$${debmes:post_error_done}
        commit_tmp_stack();
        // repost original token
        // if it still causes error, discard it;
$${debmes:repost_start}
        while ((this->*(stack_top()->entry->state))(token, value));
$${debmes:repost_done}
        if (!error_) {
            commit_tmp_stack();
        }
        if (token != ${token_eof}) {
            error_ = false;
        }
    }

)",
            {"recovery_token", options.token_prefix + options.recovery_token},
            {"token_eof", options.token_prefix + "eof"},
            {"debmes:start", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding start: stack depth = " << stack_.depth() << "\n";
)" :
                        ""}},
            {"debmes:failed", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding failed\n";
)" :
                        ""}},
            {"debmes:done", {
                    options.debug_parser ?
                        R"(        std::cerr << "recover rewinding done: stack depth = " << stack_.depth() << "\n";
)":
                        ""}},
            {"debmes:post_error_start", {
                    options.debug_parser ?
                        R"(        std::cerr << "posting error token\n";
)" :
                        ""}},
            {"debmes:post_error_done", {
                    options.debug_parser ?
                        R"(        std::cerr << "posting error token done\n";
)" :
                        ""}},
            {"debmes:repost_start", {
                    options.debug_parser ?
                        R"(        std::cerr << "reposting original token\n";
)" :
                        ""}},
            {"debmes:repost_done", {
                    options.debug_parser ? 
                        R"(        std::cerr << "reposting original token done\n";
)" :
                        ""}}
            );
    } else {
        stencil(
            os, R"(
    void recover(Token, const value_type&) {
    }

)"
            );
    }

    if (options.allow_ebnf) {
        stencil(
            os, R"(
    // EBNF support class
    struct Range {
        int beg;
        int end;
        Range() : beg(-1), end(-1) {}
        Range(int b, int e) : beg(b), end(e) {}
    };

    template <class T>
    class Sequence {
    public:
        typedef Stack<stack_frame, StackSize> stack_type;

        class const_iterator {
        public:
            typedef T value_type;

        public:
            const_iterator(SemanticAction& sa, stack_type& s, int p)
                : sa_(&sa), s_(&s), p_(p){}
            const_iterator(const const_iterator& x) : s_(x.s_), p_(x.p_){}
            const_iterator& operator=(const const_iterator& x) {
                sa_ = x.sa_;
                s_ = x.s_;
                p_ = x.p_;
                return *this;
            }
            value_type operator*() const {
                value_type v;
                sa_->downcast(v, s_->nth(p_).value);
                return v;
            }
            const_iterator& operator++() {
                ++p_;
                return *this;
            }
            bool operator==(const const_iterator& x) const {
                return p_ == x.p_;
            }
            bool operator!=(const const_iterator& x) const {
                return !((*this)==x);
            }
        private:
            SemanticAction* sa_;
            stack_type*     s_;
            int             p_;

        };

    public:
        Sequence(SemanticAction& sa, stack_type& stack, const Range& r)
            : sa_(sa), stack_(stack), range_(r) {
        }

        const_iterator begin() const {
            return const_iterator(sa_, stack_, range_.beg);
        }
        const_iterator end() const {
            return const_iterator(sa_, stack_, range_.end);
        }

    private:
        SemanticAction& sa_;
        stack_type&     stack_;
        Range           range_;

    };

    // EBNF support member functions
    bool seq_head(Nonterminal nonterminal, int base) {
        int dest = (this->*(stack_nth_top(base)->entry->gotof))(nonterminal);
        return push_stack(dest, value_type(), base);
    }

    bool seq_trail(Nonterminal, int) {
        stack_.swap_top_and_second();
        stack_top()->sequence_length++;
        return true;
    }

    Range seq_get_range(size_t base, size_t index) {
        // returns beg = end if length = 0 (includes scalar value)
        // distinguishing 0-length-vector against scalar value is
        // caller's responsibility
        int n = int(base - index);
        assert(0 < n);
        int prev_actual_index;
        int actual_index  = stack_.depth();
        while(n--) {
            actual_index--;
            prev_actual_index = actual_index;
            actual_index -= stack_.nth(actual_index).sequence_length;
        }
        return Range(actual_index, prev_actual_index);
    }

    const value_type& seq_get_arg(size_t base, size_t index) {
        Range r = seq_get_range(base, index);
        // multiple value appearing here is not supported now
        assert(r.end - r.beg == 0); 
        return stack_.nth(r.beg).value;
    }

    stack_frame* stack_nth_top(int n) {
        Range r = seq_get_range(n + 1, 0);
        // multiple value appearing here is not supported now
        assert(r.end - r.beg == 0);
        return &stack_.nth(r.beg);
    }
)"
            );
    }

    stencil(
        os, R"(
    bool call_nothing(Nonterminal nonterminal, int base) {
        pop_stack(base);
        int dest_index = (this->*(stack_top()->entry->gotof))(nonterminal);
        return push_stack(dest_index, value_type());
    }

)"
        );

    // member function signature -> index
    std::map<std::vector<std::string>, int> stub_indices;
    {
        // member function name -> count
        std::unordered_map<std::string, int> stub_counts; 

        // action handler stub
        for (const auto& pair: actions) {
            const auto& rule = pair.first;
            const auto& sa = pair.second;

            if (sa.special) {
                continue;
            }

            const auto& rule_type =
                *finder(nonterminal_types, rule.left().name());

            // make signature
            std::vector<std::string> signature;
            make_signature(
                nonterminal_types,
                rule,
                sa,
                signature);

            // skip duplicated
            if (0 < stub_indices.count(signature)) {
                continue;
            }

            // make function name
            if (stub_counts.count(sa.name) == 0) {
                stub_counts[sa.name] = 0;
            }
            int stub_index = stub_counts[sa.name];
            stub_indices[signature] = stub_index;
            stub_counts[sa.name] = stub_index+1;

            // header
            stencil(
                os, R"(
    bool call_${stub_index}_${sa_name}(Nonterminal nonterminal, int base${args}) {
)",
                {"stub_index", stub_index},
                {"sa_name", sa.name},
                {"args", [&](std::ostream& os) {
                        for (size_t l = 0 ;
                             l < sa.args.size() ; l++) {
                            os << ", int arg_index" << l;
                        }
                    }}
                );

            // check sequence conciousness
            std::string get_arg = "get_arg";
            for (const auto& arg: sa.args) {
                if (arg.type.extension != Extension::None) {
                    get_arg = "seq_get_arg";
                    break;
                }
            }

            // automatic argument conversion
            for (size_t l = 0 ; l < sa.args.size() ; l++) {
                const auto& arg = sa.args[l];
                if (arg.type.extension == Extension::None) {
                    stencil(
                        os, R"(
        ${arg_type} arg${index}; sa_.downcast(arg${index}, ${get_arg}(base, arg_index${index}));
)",
                        {"arg_type", make_type_name(arg.type)},
                        {"get_arg", get_arg},
                        {"index", l}
                        );
                } else {
                    stencil(
                        os, R"(
        ${arg_decl}; 
)",
                        {"arg_decl", make_arg_decl(arg.type, l)}
                        );
                }
            }

            // semantic action / automatic value conversion
            stencil(
                os, R"(
        ${nonterminal_type} r = sa_.${semantic_action_name}(${args});
        value_type v; sa_.upcast(v, r);
        pop_stack(base);
        int dest_index = (this->*(stack_top()->entry->gotof))(nonterminal);
        return push_stack(dest_index, v);
    }

)",
                {"nonterminal_type", make_type_name(rule_type)},
                {"semantic_action_name", sa.name},
                {"args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0 ;
                             l < sa.args.size() ; l++) {
                            if (first) { first = false; }
                            else { os << ", "; }
                            os << "arg" << l;
                        }
                    }}
                );
        }
    }

    // states handler
    for (const auto& state: table.states()) {
        // state header
        stencil(
            os, R"(
    bool state_${state_no}(token_type token, const value_type& value) {
$${debmes:state}
        switch(token) {
)",
            {"state_no", state.no},
            {"debmes:state", [&](std::ostream& os){
                    if (options.debug_parser) {
                        stencil(
                            os, R"(
        std::cerr << "state_${state_no} << " << token_label(token) << "\n";
)",
                            {"state_no", state.no}
                            );
                    }}}
            );

        // reduce action cache
        typedef boost::tuple<
            std::vector<std::string>,
            std::string,
            size_t,
            std::vector<int>>
            reduce_action_cache_key_type;
        typedef 
            std::map<reduce_action_cache_key_type,
                     std::vector<std::string>>
            reduce_action_cache_type;
        reduce_action_cache_type reduce_action_cache;

        // action table
        for (const auto& pair: state.action_table) {
            const auto& token = pair.first;
            const auto& action = pair.second;

            const auto& rule = action.rule;

            // action header 
            std::string case_tag = options.token_prefix + tokens[token];

            // action
            switch (action.type) {
                case zw::gr::action_shift:
                    stencil(
                        os, R"(
        case ${case_tag}:
            // shift
            push_stack(/*state*/ ${dest_index}, value);
            return false;
)",
                        {"case_tag", case_tag},
                        {"dest_index", action.dest_index}
                        );
                    break;
                case zw::gr::action_reduce: {
                    size_t base = rule.right().size();
                    const std::string& rule_name = rule.left().name();

                    auto k = finder(actions, rule);
                    if (k && !(*k).special) {
                        const auto& sa = *k;

                        std::vector<std::string> signature;
                        make_signature(
                            nonterminal_types,
                            rule,
                            sa,
                            signature);

                        reduce_action_cache_key_type key =
                            boost::make_tuple(
                                signature,
                                rule_name,
                                base,
                                sa.source_indices);

                        reduce_action_cache[key].push_back(case_tag);
                    } else {
                        stencil(
                            os, R"(
        case ${case_tag}:
)",
                            {"case_tag", case_tag}
                            );
                        std::string funcname = "call_nothing";
                        if (k) {
                            const auto& sa = *k;
                            assert(sa.special);
                            funcname = sa.name;
                        }
                        stencil(
                            os, R"(
            // reduce
            return ${funcname}(Nonterminal_${nonterminal}, /*pop*/ ${base});
)",
                            {"funcname", funcname},
                            {"nonterminal", rule.left().name()},
                            {"base", base}
                            );
                    }
                }
                    break;
                case zw::gr::action_accept:
                    stencil(
                        os, R"(
        case ${case_tag}:
            // accept
            accepted_ = true;
            accepted_value_ = get_arg(1, 0);
            return false;
)",
                        {"case_tag", case_tag}
                        );
                    break;
                case zw::gr::action_error:
                    stencil(
                        os, R"(
        case ${case_tag}:
            sa_.syntax_error();
            error_ = true;
            return false;
)",
                        {"case_tag", case_tag}
                        );
                    break;
            }

            // action footer
        }

        // flush reduce action cache
        for(const auto& pair: reduce_action_cache) {
            const reduce_action_cache_key_type& key = pair.first;
            const std::vector<std::string>& cases = pair.second;

            const std::vector<std::string>& signature = key.get<0>();
            const std::string& nonterminal_name = key.get<1>();
            size_t base = key.get<2>();
            const std::vector<int>& arg_indices = key.get<3>();

            for (size_t j = 0 ; j < cases.size() ; j++){
                os << ind1 << ind1 << "case " << cases[j] << ":\n";
            }

            int index = stub_indices[signature];

            stencil(
                os, R"(
            // reduce
            return call_${index}_${sa_name}(Nonterminal_${nonterminal}, /*pop*/ ${base}${args});
)",
                {"index", index},
                {"sa_name", signature[0]},
                {"nonterminal", nonterminal_name},
                {"base", base},
                {"args", [&](std::ostream& os) {
                        for(const auto& x: arg_indices) {
                            os  << ", " << x;
                        }
                    }}
                );
        }

        // dispatcher footer / state footer
        stencil(
            os, R"(
        default:
            sa_.syntax_error();
            error_ = true;
            return false;
        }
    }

)"
            );

        // gotof header
        stencil(
            os, R"(
    int gotof_${state_no}(Nonterminal nonterminal) {
)",
            {"state_no", state.no}
            );
            
        // gotof dispatcher
        std::stringstream ss;
        stencil(
            ss, R"(
        switch(nonterminal) {
)"
            );
        bool output_switch = false;
        std::unordered_set<std::string> generated;
        // TODO: ここ for (pair: state.goto_table) でよいのでは
        for(const auto& rule: table.grammar()) {
            const std::string& rule_name = rule.left().name();
            if (0 < generated.count(rule_name)) { continue; }

            if (auto k = finder(state.goto_table, rule.left())) {
                int state_index = *k;
                stencil(
                    ss, R"(
        // ${rule}
        case Nonterminal_${nonterminal}: return ${state_index};
)",
                    {"rule", [&](std::ostream& os) { os << rule; }},
                    {"nonterminal", rule_name},
                    {"state_index", state_index}
                    );
                output_switch = true;
                generated.insert(rule_name);
            }
        }

        // gotof footer
        stencil(
            ss, R"(
        default: assert(0); return false;
        }
)"
            );
        if (output_switch) {
            os << ss.str();
        } else {
            stencil(
                os, R"(
        assert(0);
        return true;
)"
                );
        }
        stencil(os, R"(
    }

)"
                );


    }

    // table
    stencil(
        os, R"(
    const table_entry* entry(int n) const {
        static const table_entry entries[] = {
$${entries}
        };
        return &entries[n];
    }

)",
        {"entries", [&](std::ostream& os) {
                int i = 0;
                for (const auto& state: table.states()) {
                    stencil(
                        os, R"(
            { &Parser::state_${i}, &Parser::gotof_${i}, ${handle_error} },
)",
                            
                        {"i", i},
                        {"handle_error", state.handle_error}
                        );
                    ++i;
                }                    
            }}
        );

    // parser class footer
    // namespace footer
    // once footer
    stencil(
        os,
        R"(
};

} // namespace ${namespace_name}

#endif // #ifndef ${headername}_

)",
        {"headername", {headername}},
        {"namespace_name", {options.namespace_name}}
        );
}
