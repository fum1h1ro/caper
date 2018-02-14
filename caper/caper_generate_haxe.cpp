// Copyright (C) 2008 Naoyuki Hirayama.
// All Rights Reserved.

// $Id$

#include "caper_ast.hpp"
#include "caper_generate_haxe.hpp"
#include "caper_format.hpp"
#include "caper_stencil.hpp"
#include "caper_finder.hpp"
#include <algorithm>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/filesystem/path.hpp>

namespace {

std::string capitalize_token(const std::string s) {
    if (s == "eof") {
        return "Eof";
    } else if (s == "error") {
        return "Error";
    } else {
        return s;
    }
}

std::string make_type_name(const Type& x) {
    switch(x.extension) {
        case Extension::None:
            return x.name;
        case Extension::Star:
        case Extension::Plus:
        case Extension::Slash:
            return "Array<" + x.name + ">";
        case Extension::Question:
            return "Null<" + x.name + ">";
        default:
            assert(0);
            return "";
    }
}
        
std::string make_arg_decl(const Type& x, size_t l) {
    std::string sl = std::to_string(l);
    std::string y = "var arg" + sl;
    switch (x.extension) {
        case Extension::None:
            assert(0);
            return "";
        case Extension::Star:
        case Extension::Plus:
        case Extension::Slash:
            return
                y + " = seqGetSequence(base, argIndex" + sl + ")";
        case Extension::Question:
            return
                y + " = seqGetOptional(base, argIndex" + sl + ")";
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

void make_generics_parameters(
    std::ostream& os, const std::map<std::string, Type>& nonterminal_types) {

    bool first = true;
    for (const auto& nonterminal_type: nonterminal_types) {
        if (first) { first = false; }
        else { os << ", "; }
        os << nonterminal_type.first;
    }
}

} // unnamed namespace

void generate_haxe(
    const std::string&                  src_filename,
    std::ostream&                       os,
    const GenerateOptions&              options,
    const std::map<std::string, Type>&,
    const std::map<std::string, Type>&  nonterminal_types,
    const std::vector<std::string>&     tokens,
    const action_map_type&              actions,
    const tgt::parsing_table&           table) {

    // notice / URL / module / imports
    stencil(
        os, R"(
// This file was automatically generated by Caper.
// (http://jonigata.github.io/caper/caper.html)

${package}

import haxe.ds.Option;

)",
        {"package",
            { options.namespace_name == "" ? "" : "package "+options.namespace_name+";" }}
        );

    if (!options.external_token) {
        // token enumeration
        stencil(
            os, R"(
enum Token {
$${tokens}
}

class TokenLabels {
    public static function get(t: Token): String {
        return switch(t) {
$${labels}
        }
    }
}

)",
            {"tokens", [&](std::ostream& os){
                    for(const auto& token: tokens) {
                        stencil(
                            os, R"(
    ${token};
)",
                            {"token", capitalize_token(token)}
                            );
                    }
                }},
            {"labels", [&](std::ostream& os){
                    for(const auto& token: tokens) {
                        stencil(
                            os, R"(
            case ${token}: "${token}";
)",
                            {"token", capitalize_token(token)}
                            );
                    }
                }}
            );

    }

    // stack
    stencil(
        os, R"(
class Stack<T> {
    var stack: Array<T> = [];
    var tmp: Array<T> = [];
    var gap: Int;

    public function new() { this.gap = 0; }

    public function rollbackTmp() {
        this.gap = this.stack.length;
        this.tmp = [];
    }

    public function commitTmp() {
        this.stack.splice(gap, this.stack.length - gap);

        for (e in this.tmp) {
            this.stack.push(e);
        }
        this.tmp = [];
    }
    public function push(f: T): Bool {
        this.tmp.push(f);
        return true;
    }
	   
    public function pop(n: Int) {
        if (this.tmp.length < n) {
            n -= this.tmp.length;
            this.tmp = [];
            this.gap -= n;
        } else {
            this.tmp.splice(-n, n);
        }
    }

    public function top(): T {
        //assert(0 < depth());
        if (0 < this.tmp.length) {
            return this.tmp[this.tmp.length - 1];
        } else {
            return this.stack[this.gap - 1];
        }
    }
	   
    public function getArg(base: Int, index: Int): T {
        var n = tmp.length;
        if (base - index <= n) {
            return this.tmp[n - (base - index)];
        } else {
            return this.stack[this.gap - (base - n) + index];
        }
    }
	   
    public function clear() {
        this.stack = [];
        this.tmp = [];
        this.gap = 0; 
    }
	   
    public function empty(): Bool {
        if (0 < this.tmp.length) {
            return false;
        } else {
            return this.gap == 0;
        }
    }
	   
    public function depth(): Int {
        return this.gap + this.tmp.length;
    }
	   
    public function nth(index: Int): T {
        if (this.gap <= index) {
            return this.tmp[index - this.gap];
        } else {
            return this.stack[index];
        }
    }

    public function setNth(index: Int, t: T) {
        if (this.gap <= index) {
            this.tmp[index - this.gap] = t;
        } else {
            this.stack[index] = t;
        }
    }

    public function swapTopAndSecond() {
        var d = depth();
        //assert(2 <= d);
        var x = nth(d - 1);
        setNth(d - 1, nth(d - 2));
        setNth(d - 2, x);
    }

}

)");

    // parser class header
    stencil(
        os, R"(
private typedef TableEntry = {
    state: Int,
    gotof: Int,
    handleError: Bool,
};

private typedef StackFrame<${generics_parameters}> = {
    entry: TableEntry,
    value: Dynamic,
    sequenceLength: Int,
};

private typedef Range = {
    begin: Int,
    end: Int,
};

private enum Nonterminal {
)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }}
        );


    for (const auto& nonterminal_type: nonterminal_types) {
        stencil(
            os, R"(
    ${nonterminal_name};
)",
{ "nonterminal_name", "Nonterminal_" + nonterminal_type.first }
            );
    }
    

    stencil(
        os, R"(
}
)"
        );
    
    stencil(
        os, R"(
private typedef SemanticAction<${generics_parameters}> = {
    function syntaxError(): Void;
    function stackOverflow(): Void;
)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }}
        );
    
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

            // make function name
            if (stub_counts.count(sa.name) == 0) {
                stub_counts[sa.name] = 1;
            } else {
                continue;
            }

            // header
            stencil(
                os, R"(
    function ${sa_name}(${args}): ${nonterminal_type};
)",
                {"nonterminal_type", make_type_name(rule_type)},
                {"sa_name", sa.name},
                {"args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
                            const auto& arg = sa.args[l];
                            if (first) { first = false; }
                            else { os << ", "; }
                            os << "arg" << l << ": "
                               << make_type_name(arg.type);
                        }
                    }}
                );
        }
    }

    stencil(
        os, R"(
}

class Parser<${generics_parameters}> {

    public function new(sa: SemanticAction<${generics_parameters}>){ this.sa = sa; reset(); }

    public function reset() {
        this.failed = false;
        this.accepted = false;
        this.stack = new Stack();
        rollbackTmpStack();
        if (pushStack(${first_state}, null)) {
            commitTmpStack();
        } else {
            this.sa.stackOverflow();
            this.failed = true;
        }
    }

    public function post(token: Token, value: Dynamic): Bool {
        rollbackTmpStack();
        this.failed = false;
        while(state_table(stackTop().entry.state, this, token, value)){ }
        if (!this.failed) {
            commitTmpStack();
        } else {
            recover(token, value);
        }
        return this.accepted || this.failed;
    }

    public function accept(): Dynamic {
        //assert(this.accepted);
        if (this.failed) { return null; }
        return this.acceptedValue;
    }

    public function error(): Bool { return this.failed; }

)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }},
        {"first_state", table.first_state()}
        );

    // implementation
    stencil(
        os, R"(

    var accepted: Bool;
    var failed: Bool;
    var acceptedValue: Dynamic;

    var sa: SemanticAction<${generics_parameters}>;

)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }}
        );

    // stack operation
    stencil(
        os, R"(
    var stack: Stack<StackFrame<${generics_parameters}>>;

    function pushStack(stateIndex: Int, v: Dynamic, sl: Int = 0): Bool {
	var f = this.stack.push({
            entry: entry(stateIndex),
            value: v,
            sequenceLength: sl
        });
        //assert(!this.failed);
        if (!f) { 
            this.failed = true;
            this.sa.stackOverflow();
        }
        return f;
    }

    function popStack(n: Int) {
$${pop_stack_implementation}
    }

    function stackTop(): StackFrame<${generics_parameters}> {
        return this.stack.top();
    }

    function getArg(base: Int, index: Int): Dynamic {
        return this.stack.getArg(base, index).value;
    }

    function clearStack() {
        this.stack.clear();
    }

    function rollbackTmpStack() {
        this.stack.rollbackTmp();
    }

    function commitTmpStack() {
        this.stack.commitTmp();
    }

)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }},
        {"pop_stack_implementation", [&](std::ostream& os) {
                if (options.allow_ebnf) {
                    stencil(
                        os, R"(
        var nn = n;
        while(0 < nn--) {
            this.stack.pop(1 + this.stack.top().sequenceLength);
        }
)"
                        );
                } else {
                    stencil(
                        os, R"(
        this.stack.pop(n);
)"
                        );
                }
            }}
        );

    if (options.recovery) {
        stencil(
            os, R"(
    function recover(token: Token, value: Dynamic) {
        rollbackTmpStack();
        this.failed = false;
$${debmes:start}
        while(!stackTop().entry.handleError) {
            popStack(1);
            if (this.stack.empty()) {
$${debmes:failed}
                this.failed = true;
                return;
            }
        }
$${debmes:done}
        // post error_token;
$${debmes:post_error_start}
        while(state_table(stackTop().entry.state, this, Token.${recovery_token}, null)){}
$${debmes:post_error_done}
        commitTmpStack();
        // repost original token
        // if it still causes error, discard it;
$${debmes:repost_start}
        while(state_table(stackTop().entry.state, this, token, value)){ }
$${debmes:repost_done}
        if (!this.failed) {
            commitTmpStack();
        }
        if (token != Token.${token_eof}) {
            this.failed = false;
        }
    }

)",
            {"recovery_token", capitalize_token(options.recovery_token)},
            {"token_eof", capitalize_token("eof")},
            {"debmes:start", {
                    options.debug_parser ?
                        R"(        trace('recover rewinding start: stack depth = ' + this.stack.depth());
)" :
                        ""}},
            {"debmes:failed", {
                    options.debug_parser ?
                        R"(        trace("recover rewinding failed");
)" :
                        ""}},
            {"debmes:done", {
                    options.debug_parser ?
                        R"(        trace('recover rewinding done: stack depth = ' + this.stack.depth());
)":
                        ""}},
            {"debmes:post_error_start", {
                    options.debug_parser ?
                        R"(        trace("posting error token");
)" :
                        ""}},
            {"debmes:post_error_done", {
                    options.debug_parser ?
                        R"(        trace("posting error token done");
)" :
                        ""}},
            {"debmes:repost_start", {
                    options.debug_parser ?
                        R"(        trace("reposting original token");
)" :
                        ""}},
            {"debmes:repost_done", {
                    options.debug_parser ? 
                        R"(        trace("reposting original token done");
)" :
                        ""}}
            );
    } else {
        stencil(
            os, R"(
    function recover(t: Token, v: Dynamic) {
    }

)"
            );
    }

    if (options.allow_ebnf) {
        stencil(
            os, R"(
    // EBNF support member functions
    function seqHead(nonterminal: Nonterminal, base: Int): Bool {
        // case '*': base == 0
        // case '+': base == 1
        var dest = gotof_table(stackNthTop(base).entry.gotof, nonterminal);
        return pushStack(dest, null, base);
    }
    function seqTrail(nonterminal: Nonterminal, base: Int): Bool {
        // '*', '+' trailer
        this.stack.swapTopAndSecond();
        stackTop().sequenceLength++;
        return true;
    }
    function seqTrail2(nonterminal: Nonterminal, base: Int): Bool {
        // '/' trailer
        this.stack.swapTopAndSecond();
        popStack(1); // erase delimiter
        this.stack.swapTopAndSecond();
        stackTop().sequenceLength++;
        return true;
    }
    function optNothing(nonterminal: Nonterminal, base: Int): Bool {
        // same as head of '*'
        return seqHead(nonterminal, base);
    }
    function optJust(nonterminal: Nonterminal, base: Int): Bool {
        // same as head of '+'
        return seqHead(nonterminal, base);
    }
    function seqGetRange(base: Int, index: Int): Range {
        // returns beg = end if length = 0 (includes scalar value)
        // distinguishing 0-length-vector against scalar value is
        // caller's responsibility
        var n = base - index;
        var prevActualIndex = 0;
        var actualIndex = this.stack.depth();
        while(0 < n--) {
            actualIndex--;
            prevActualIndex = actualIndex;
            actualIndex -= this.stack.nth(actualIndex).sequenceLength;
        }
        return { begin: actualIndex, end: prevActualIndex};
    }
    function seqGetArg(base: Int, index: Int): Dynamic {
        var r = seqGetRange(base, index);
        // multiple value appearing here is not supported now
        return this.stack.nth(r.begin).value;
    }
    function seqGetOptional(base: Int, index: Int): Dynamic {
        var r = seqGetRange(base, index);
        if (r.begin == r.end) { return null; }
        return this.stack.nth(r.begin);
    }
    function seqGetSequence<T>(base: Int, index: Int): Array<T> {
        var r = seqGetRange(base, index);
        var a = new Array<T>();
        if (r.begin == r.end) { return null; }
        for (i in r.begin...r.end) {
            a.push(this.stack.nth(i).value);
        }
        return a;
    }
    function stackNthTop(n: Int): StackFrame<${generics_parameters}> {
        var r = this.seqGetRange(n + 1, 0);
        // multiple value appearing here is not supported now
        return this.stack.nth(r.begin);
    }

    function opt_nothing(nonterminal: Nonterminal, base: Int): Bool {
        return optNothing(nonterminal, base);
    }
    function opt_just(nonterminal: Nonterminal, base: Int): Bool {
        return optJust(nonterminal, base);
    }
    function seq_head(nonterminal: Nonterminal, base: Int): Bool {
        return seqHead(nonterminal, base);
    }
    function seq_trail(nonterminal: Nonterminal, base: Int): Bool {
        return seqTrail(nonterminal, base);
    }
    function seq_trail2(nonterminal: Nonterminal, base: Int): Bool {
        return seqTrail2(nonterminal, base);
    }

)",
            {"generics_parameters", [&](std::ostream& os) {
                    make_generics_parameters(os, nonterminal_types);
                }}
            );
    }


    stencil(
        os, R"(
    function call_nothing(nonterminal: Nonterminal, base: Int): Bool {
        popStack(base);
        var dest_index = gotof_table(stackTop().entry.gotof, nonterminal);
        return pushStack(dest_index, null);
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
    function call_${stub_index}_${sa_name}(nonterminal: Nonterminal, base: Int${args}): Bool {
)",
                {"stub_index", stub_index},
                {"sa_name", sa.name},
                {"args", [&](std::ostream& os) {
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
                            os << ", argIndex" << l << ": Int";
                        }
                    }}
                );

            // check sequence conciousness
            std::string get_arg = "getArg";
            for (const auto& arg: sa.args) {
                if (arg.type.extension != Extension::None) {
                    get_arg = "seqGetArg";
                    break;
                }
            }

            // automatic argument conversion
            for (size_t l = 0 ; l < sa.args.size() ; l++) {
                const auto& arg = sa.args[l];
                if (arg.type.extension == Extension::None) {
                    stencil(
                        os, R"(
        var arg${index} = ${get_arg}(base, argIndex${index});
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
        var r = this.sa.${semantic_action_name}(${args});
        popStack(base);
        var dest_index = gotof_table(stackTop().entry.gotof, nonterminal);
        return pushStack(dest_index, r);
    }

)",
                {"nonterminal_type", make_type_name(rule_type)},
                {"semantic_action_name", sa.name},
                {"args", [&](std::ostream& os) {
                        bool first = true;
                        for (size_t l = 0 ; l < sa.args.size() ; l++) {
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
    static function state_${state_no}<${generics_parameters}>(self:Parser<${generics_parameters}>, token: Token, value:  Dynamic): Bool {
$${debmes:state}
        switch(token) {
)",
            {"state_no", state.no},
            {"debmes:state", [&](std::ostream& os){
                    if (options.debug_parser) {
                        stencil(
                            os, R"(
        trace('state_${state_no} << ' + TokenLabels.get(token));
)",
                            {"state_no", state.no}
                            );
                    }}},
			{ "generics_parameters", [&](std::ostream& os) {
					make_generics_parameters(os, nonterminal_types);
			} }
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
            std::string case_tag = capitalize_token(tokens[token]);

            // action
            switch (action.type) {
                case zw::gr::action_shift:
                    stencil(
                        os, R"(
        case ${case_tag}:
            // shift
            self.pushStack(/*state*/ ${dest_index}, value);
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
            return self.${funcname}(${nonterminal}, /*pop*/ ${base});
)",
                            {"funcname", funcname},
                            {"nonterminal", "Nonterminal_"+rule.left().name()},
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
            self.accepted = true;
            self.acceptedValue = self.getArg(1, 0);
            return false;
)",
                        {"case_tag", case_tag}
                        );
                    break;
                case zw::gr::action_error:
                    stencil(
                        os, R"(
        case ${case_tag}:
            self.sa.syntaxError();
            self.failed = true;
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

            os << "        case ";
            for (size_t j = 0 ; j < cases.size() ; j++){
                // fall through, be aware when port to other language
                os << cases[j];
                if (j < cases.size() -1) {
                    os << " | ";
                } else {
                    os << ":\n";
                }
            }

            int index = stub_indices[signature];

            stencil(
                os, R"(
            // reduce
            return self.call_${index}_${sa_name}(${nonterminal}, /*pop*/ ${base}${args});
)",
                {"index", index},
                {"sa_name", signature[0]},
                {"nonterminal", "Nonterminal_"+nonterminal_name},
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
            self.sa.syntaxError();
            self.failed = true;
            return false;
        }
    }

)"
            );

        // gotof header
        stencil(
            os, R"(
    static function gotof_${state_no}(nonterminal: Nonterminal): Int {
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
        for (const auto& pair: state.goto_table) {
            stencil(
                ss, R"(
        case Nonterminal_${nonterminal}: return ${state_index};
)",
                {"nonterminal", pair.first.name()},
                {"state_index", pair.second}
                );
            output_switch = true;
        }

        // gotof footer
        stencil(
            ss, R"(
        default: /*assert(0);*/ return -1;
        }
)"
            );
        if (output_switch) {
            os << ss.str();
        } else {
            stencil(
                os, R"(
        //assert(0);
        return -1;
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
    static var entries = [
$${entries}
    ];

    function entry(n: Int): TableEntry {
        return entries[n];
    }

)",
        {"generics_parameters", [&](std::ostream& os) {
                make_generics_parameters(os, nonterminal_types);
            }},
        {"entries", [&](std::ostream& os) {
                int i = 0;
                for (const auto& state: table.states()) {
                    stencil(
                        os, R"(
        { state: ${i}, gotof: ${i}, handleError: ${handle_error} },
)",
                            
                        {"i", i},
                        {"handle_error", state.handle_error}
                        );
                    ++i;
                }                    
            }}
        );

		stencil(
			os,
			R"(
	static function state_table<${generics_parameters}>(index:Int, self:Parser<${generics_parameters}>, token:Token, value:Dynamic):Bool {
		return switch index {
$${states}
			case _: throw "state_table faild.";
		}
	}
)", { "generics_parameters", [&](std::ostream& os) {
		make_generics_parameters(os, nonterminal_types);
	} },
	{ "states", [&](std::ostream& os){
		int i = 0;
		for (const auto& state : table.states()){
			stencil(
				os, R"(
			case ${i}: state_${i}(self, token, value);
)", { "i", i });
			++i;
		}
	}}
        );

		stencil(
			os,
			R"(
	static function gotof_table(index:Int, nonterminal: Nonterminal):Int {
		return switch index {
$${gotofs}
			case _: throw "gotof_table faild.";
		}
	}
)", { "gotofs", [&](std::ostream& os){
			int i = 0;
			for (const auto& state : table.states()){
				stencil(
					os, R"(
			case ${i}: gotof_${i}(nonterminal);
)", { "i", i });
				++i;
			}
		} }
		);

    // parser class footer
    // namespace footer
    // once footer
    stencil(
        os,
        R"(
}

)"
        );
}
