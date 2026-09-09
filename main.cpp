#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
// #include <llvm/IR/Verifier.h>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <deque>
// #include <stacktrace>
#include <variant>

using namespace llvm;

typedef std::pair<std::string, std::string> token;
typedef std::deque<token> toklist;
typedef std::function<Value*(Value*, Value*)> OpHandler;
typedef std::function<Value*()> ExprFn;

void error_msg(std::string error, int pos) {
    std::cerr << "\033[31m" << '[' << "POS:" << pos << "] " << error << "\033[0m" << std::endl;
    std::exit(1);
}

template <typename T1, typename T2, typename T3>
struct triple {
    T1 first;
    T2 second;
    T3 third;  
};

class Farx {
    public:
        LLVMContext context;
        Module* module;
        std::unique_ptr<IRBuilder<>> builder;

        std::unordered_map<std::string, Type*> types;
        std::unordered_set<std::string> ops;
        std::unordered_map<std::string, std::pair<int, OpHandler>> op_handlers;
        std::unordered_map<std::string, std::function<void()>> keywords;
        std::unordered_map<std::string, std::function<Value*()>> pre_ops;

        Function* cur_func;
        std::string cur_nmsp;

        std::unordered_map<std::string, triple<Value*, Type*, bool>> scope;
        std::unordered_map<std::string, Function*> funcs;
        std::unordered_map<std::string, BasicBlock*> labels;

        toklist tokens;
        int pos = 0;

        std::unordered_set<char> solo_tokens = {'(', ')', ',', '[', ']', ':', ';', 
                                        '+', '-', '*', '/', '%', '^', '&', 
                                        '~', '{', '}', '='};
        std::unordered_set<char> skip_tokens = {'\n', '\t', ' '};
        std::unordered_map<std::string, std::string> special_tokens;
        
        
        Farx(): module(nullptr), cur_func(nullptr), cur_nmsp(""), pos(0) {
            module = new Module("Farx", context);
            builder = std::make_unique<IRBuilder<>>(context);
            
            types["int1"] = Type::getInt1Ty(context);
            types["int8"] = Type::getInt8Ty(context);
            types["int"] = Type::getInt32Ty(context);
            types["int64"] = Type::getInt64Ty(context);
            types["void"] = Type::getVoidTy(context);
            types["ptr"] = PointerType::get(context, 0);
            types["str"] = PointerType::get(context, 0);

            ops = {
                "<<", ">>", "!=", "==", "<=", ">=",
                "=", "+", "-", "*", "/", "%",
                "&", "|", "~", "^", "<", ">"
            };

            op_handlers["+"] = {13, [this](Value* a, Value* b) { return builder->CreateAdd(a, b); }};
            op_handlers["-"] = {13, [this](Value* a, Value* b) { return builder->CreateSub(a, b); }};
            op_handlers["*"] = {14, [this](Value* a, Value* b) { return builder->CreateMul(a, b); }};
            op_handlers["/"] = {14, [this](Value* a, Value* b) { return builder->CreateSDiv(a, b); }};

            op_handlers["&"] = {8, [this](Value* a, Value* b) { return builder->CreateAnd(a, b); }};
            op_handlers["|"] = {6, [this](Value* a, Value* b) { return builder->CreateOr(a, b); }};
            op_handlers["^"] = {7, [this](Value* a, Value* b) { return builder->CreateXor(a, b); }};
            op_handlers["<<"] = {12, [this](Value* a, Value* b) { return builder->CreateShl(a, b); }};
            op_handlers[">>"] = {12, [this](Value* a, Value* b) { return builder->CreateAShr(a, b); }};

            op_handlers["<"] = {10, [this](Value* a, Value* b) { return builder->CreateICmpSLT(a, b); }};
            op_handlers[">"] = {10, [this](Value* a, Value* b) { return builder->CreateICmpSGT(a, b); }};
            op_handlers["!="] = {9, [this](Value* a, Value* b) { return builder->CreateICmpNE(a, b); }};
            op_handlers["=="] = {9, [this](Value* a, Value* b) { return builder->CreateICmpEQ(a, b); }};
            op_handlers["<="] = {10, [this](Value* a, Value* b) { return builder->CreateICmpSLE(a, b); }};
            op_handlers[">="] = {10, [this](Value* a, Value* b) { return builder->CreateICmpSGE(a, b); }};

            keywords["namespace"] = [this]() { this->namespace_stmt(); };
            keywords["return"] = [this]() { this->return_stmt(); };
            keywords["struct"] = [this]() { this->struct_stmt(); };
            keywords["while"] = [this]() { this->while_stmt(); };
            keywords["label"] = [this]() { this->label_stmt(); };
            keywords["goto"] = [this]() { this->goto_stmt(); };
            keywords["else"] = [this]() { error_msg("'else' cannot be used without 'if'", this->pos); };
            keywords["fn"] = [this]() { this->fn_stmt(); };
            keywords["if"] = [this]() { this->if_stmt(); };

            pre_ops["*"] = [this]() { return ptr_to(); };
            pre_ops["&"] = [this]() { return link_to(); };
            pre_ops["~"] = [this]() { return not_to(); };

            special_tokens["("] = "LPAREN";
            special_tokens[")"] = "RPAREN";
            special_tokens["["] = "LBRACKET";
            special_tokens["]"] = "RBRACKET";
            special_tokens["{"] = "LBRACE";
            special_tokens["}"] = "RBRACE";
            special_tokens[","] = "COMMA";
            special_tokens[";"] = "SEMI";
        }

        ~Farx() {
            delete module;
        }

        // Pre ops ---
        Value* ptr_to() {
            Value* ptr = parse_expr(100)();
            return builder->CreateLoad(types["int"], ptr);
        }

        Value* link_to() {
            int links = 1;

            while (peek().first == "OP" && peek().second == "&") {
                consume("OP"); // &
                links++;
            }

            std::string name = consume("IDENT", "Expected name of variable to create pointer").second;

            Value* ptr = scope[name].first;
            Type* _type = scope[name].second;

            for (int i = 0; i < links; i++) {
                PointerType* typ = PointerType::get(context, 0);
                Value* tmp = builder->CreateAlloca(typ, nullptr, "tmp");
                builder->CreateStore(ptr, tmp);
                ptr = tmp;
            }
            return ptr;
        }

        Value* not_to() { // Will stay as a function for now
            return builder->CreateNot(parse_expr(100)());
        }
        // -----------


        // Statements
        void namespace_stmt() {
            std::string name = consume("IDENT", "Expected name of namespace").second;

            if (cur_nmsp != "") {
                cur_nmsp += "::";
            }
            cur_nmsp += name;

            consume("LBRACE", "Expected start of the namespace block");
            while (peek().first != "RBRACE") {
                parse_stmt();
            }
            consume("RBRACE", "Expected end of the namespace block");

            std::string suffix = "::" + name;
            if (cur_nmsp.length() >= suffix.length()) {
                cur_nmsp = cur_nmsp.substr(0, cur_nmsp.length() - suffix.length());
            }
            if (cur_nmsp.length() >= 2 && cur_nmsp.substr(cur_nmsp.length() - 2) == "::") {
                cur_nmsp = cur_nmsp.substr(0, cur_nmsp.length() - 2);
            }
            if (cur_nmsp == name) {
                cur_nmsp.clear();
            }
        }

        void return_stmt() {
            Value* val = parse_expr(0)();
            builder->CreateRet(val);
        }
        
        void create_struct_type(std::string& name, std::vector<std::pair<std::string, int>> fields) {
            std::vector<Type*> field_llvm_types;

            for (auto& field: fields) {
                if (field.second != 0) {
                    ArrayType* arr_type = ArrayType::get(types[field.first], field.second);
                    field_llvm_types.push_back(arr_type);
                } else {
                    Type* _type = types[field.first];
                    field_llvm_types.push_back(_type);
                }
            }
            StructType* struct_ty = StructType::create(module->getContext(), field_llvm_types, name);
            types[name] = struct_ty;
        }

        void struct_stmt() {
            std::string name = consume("IDENT", "Expected name of new struct type").second;
            std::vector<std::pair<std::string, int>> temp_dict;

            if (peek().second == ";") {
                consume("SEMI", "Expected ';' at the end of struct prototype"); // ;
                create_struct_type(name, temp_dict);
            } else {
                consume("LBRACE", "Expected '{' at the start of the struct type declaration"); // {
                while (peek().first != "RBRACE") {
                    std::string _type = consume("IDENT", "Expected type in the struct block").second;
                    if (peek().first == "LBRACKET") {
                        consume("LBRACKET"); // [
                        int obj = parse_expr(0)()->ConstantIntVal;
                        consume("RBRACKET", "Expected ']' at the end of array declaration"); // ]
                        temp_dict.push_back({_type, obj});
                    } else {
                        temp_dict.push_back({_type, 0});
                    }
                }
            }
            create_struct_type(name, temp_dict);
            consume("RBRACE", "Expected '}' at the end of struct type declaration");
        }

        void while_stmt() {
            BasicBlock* cond = BasicBlock::Create(cur_func->getContext(), "while.cond", cur_func);
            BasicBlock* body = BasicBlock::Create(cur_func->getContext(), "while.body", cur_func);
            BasicBlock* end = BasicBlock::Create(cur_func->getContext(), "while.end", cur_func);

            builder->CreateBr(cond);

            builder->SetInsertPoint(cond);
            consume("LPAREN", "Expected '(' at the start of the condition block"); // (
            Value* cond_val = parse_expr(0)();
            consume("RPAREN", "Expected ')' at the end of condition block"); // )

            builder->CreateCondBr(cond_val, body, end);

            builder->SetInsertPoint(body);
            consume("LBRACE", "Expected '{' at the start of body block");
            while (peek().first != "RBRACE") {
                parse_stmt();
            }
            consume("RBRACE", "Expected '}' at the end of body block");

            builder->CreateBr(cond);
            builder->SetInsertPoint(end);
        }

        void label_stmt() {
            std::string name = consume("IDENT", "Expected name of the new label block").second;
            consume(); // :
            BasicBlock* label_block = BasicBlock::Create(cur_func->getContext(), name, cur_func);
            builder->CreateBr(label_block);
            builder->SetInsertPoint(label_block);
            labels[name] = label_block; // Creating label in the current label scope
        }

        void goto_stmt() {
            std::string name = consume("IDENT").second;
            builder->CreateBr(labels[name]); // goto to label with "name" in current label scope
        }

        void fn_stmt() {
            bool no_args = false;
            std::string func_name = consume("IDENT", "Expected name of the new function").second;
            if (cur_nmsp != "") {
                func_name = cur_nmsp + "::" + func_name;
            }
            std::vector<std::tuple<std::string, Type*>> args;
            std::vector<Type*> arg_types;
            std::string ret_type_name;

            // Check if there's no args
            if (peek().second == ":") {
                consume();
                no_args = true;
                ret_type_name = consume("IDENT", "Expected return type of the function").second;
            } else {
                consume("LPAREN", "Expected '(' at the start of function argument list"); // (
                while (peek().second != ")") {

                    // if (peek().second == "<*>") {} not implemented...
                    std::string arg_type_name = consume("IDENT", "Expected type of the argument").second;
                    Type* arg_type = types[arg_type_name];

                    arg_type = _parse_pointer(arg_type);

                    std::string arg_name = consume("IDENT", "Expected name of the argument").second;
                    args.push_back({arg_name, arg_type});
                    if (peek().first == "COMMA") {
                        consume(); // ,
                    }
                }
                consume("RPAREN", "Expected ')' at the end of the function argument list"); // )
                consume(); // :
                ret_type_name = consume("IDENT", "Expected return type of the function").second;
            }


            for (const auto& arg: args) {
                Type* _type = std::get<1>(arg); // arg name
                arg_types.push_back(_type);
            }

            Type* ret_type = types[ret_type_name];
            FunctionType* func_type = FunctionType::get(ret_type, arg_types, false);
            cur_func = Function::Create(func_type, Function::ExternalLinkage, func_name, module);

            auto variables_snapshot = scope;
            funcs[func_name] = cur_func;

            // Check if this is a function prototype
            if (peek().second == ";") {
                consume("SEMI", "Expected ';' at the end of function prototype"); // ;
                scope.clear();
            } else {
                BasicBlock* block = BasicBlock::Create(module->getContext(), "entry", cur_func);
                builder = std::make_unique<IRBuilder<>>(block);
                scope.clear();

                if (!no_args) {
                    int i = 0;
                    for (const auto& arg: args) {
                        const auto& name = std::get<0>(arg);
                        Type* _type = std::get<1>(arg);
                        scope[name] = {cur_func->getArg(i), _type, true};
                        i++;
                    }
                }

                for (const auto& var: variables_snapshot) {
                    if (!scope.count(var.first)) {
                        scope[var.first] = var.second;
                    }
                }

                consume("LBRACE", "Expected '{' at the start of the function body block"); // {
                while (peek().second != "" && peek().first != "RBRACE") {
                    parse_stmt();
                }
                consume("RBRACE", "Expected '}' at the end of the function body block"); // }
                if (!builder->GetInsertBlock()->getTerminator()) {
                    if (ret_type == Type::getVoidTy(module->getContext())) {
                        builder->CreateRetVoid();
                    } else {
                        Constant* zero = Constant::getNullValue(ret_type);
                        builder->CreateRet(zero);
                    }
                }
                builder = nullptr;
                labels.clear();
                scope = variables_snapshot;
            }
        }

        void if_stmt() {
            consume("LPAREN", "Expected '(' at the start of the condition block");
            Value* cond = parse_expr(0)();
            consume("RPAREN", "Expected ')' at the end of condition block");

            consume("LBRACE", "Expected '{' at the start of 'if' body block");

            BasicBlock* then = BasicBlock::Create(cur_func->getContext(), "if.then", cur_func);
            BasicBlock* _else = BasicBlock::Create(cur_func->getContext(), "if.else", cur_func);
            BasicBlock* merge = BasicBlock::Create(cur_func->getContext(), "if.merge", cur_func);

            builder->CreateCondBr(cond, then, _else);
            builder->SetInsertPoint(then);

            while (peek().first != "RBRACE") {
                parse_stmt();
            }

            consume("RBRACE", "Expected '}' at the end of 'if' body block");
            builder->CreateBr(merge);

            builder->SetInsertPoint(_else);
            if (peek().second == "else") {
                consume(); // else
                consume("LBRACE", "Expected '{' at the start of 'else' body block");
                while (peek().first != "RBRACE") {
                    parse_stmt();
                }
                consume("RBRACE", "Expected '}' at the end of 'else' body block");
            }
            builder->CreateBr(merge);
            builder->SetInsertPoint(merge);
        }
        // ----------


        // Basic things
        token peek() {
            return tokens[pos];
        }

        token consume(std::string expected="null", std::string err_msg="null") {
            token tok = peek();
            // std::cerr << std::stacktrace::current()[1] << std::endl; <--- if need debug
        
            if (expected != "null") { // I dont trust nullptr here
                if (tok.first != expected) {
                    error_msg(err_msg, pos);
                }
            }
            pos++;
            return tok;
        }
        // -----------


        // Parser
        std::deque<std::string> split_text(std::string text) {
            std::deque<std::string> tokens;
            std::string current;
            bool str = false;

            for (int i = 0; i < text.size(); i++) {
                char c = text[i];
                if (c == '"') {
                    current += '"';
                    str = !str;
                } else if (c == '/' && text[i+1] == '/') {
                    while (text[i] != '\n') { // Comments
                        i++;
                    }
                } else if (c == ':' && text[i+1] == ':') {
                    current += "::";
                    i++;
                } else if (solo_tokens.count(c) && !str) {
                    if (current != "") {
                        tokens.push_back(current);
                    }
                    current.clear();

                    current += c;

                    tokens.push_back(current);
                    current.clear();
                } else if (c == ' ' && !str && current != "") {
                    tokens.push_back(current);
                    current.clear();
                } else {
                    if (!str) {
                        if (!skip_tokens.count(c)) {
                            current += c;
                        }
                    } else {
                        current += c;
                    }
                }
            }

            if (current != "" && current != " ") { // If it is an empty token - skip
                tokens.push_back(current);
            }

            return tokens;
        }

        toklist parse_tokens(std::deque<std::string> tokens) {
            std::deque<std::pair<std::string, std::string>> parsed_tokens;
            int skip = 0;
            int pos = 0;
            for (auto& token: tokens) {
                if (skip) {
                    skip--;
                } else {
                    if (ops.count(token + tokens[pos + 1])) {
                        parsed_tokens.push_back({"OP", token+tokens[pos+1]});
                        skip++;
                    } else if (ops.count(token)) {
                        parsed_tokens.push_back({"OP", token});
                    } else if (keywords.count(token)) {
                        parsed_tokens.push_back({"KEYWORD", token});
                    } else if (token[0] == '"') {
                        token.erase(0, 1); token.erase(token.length() - 1, token.length());
                        parsed_tokens.push_back({"STR", token});
                    } else if (special_tokens.count(token)) {
                        parsed_tokens.push_back({special_tokens[token], token});
                    } else {
                        try {
                            int _ = std::stoi(token); //! Will be replaced (or not) with other way to check if its digit
                            parsed_tokens.push_back({"NUMBER", token});
                        } catch (...) {
                            parsed_tokens.push_back({"IDENT", token});
                        }
                    }
                }
                pos++;
            }
            parsed_tokens.push_back({"EOF", ""});
            return parsed_tokens;
        }

        toklist parse_namespaces(toklist tokens) {
            toklist out_tokens;
            int skip = 0;
            int pos = 0;

            for (auto& token: tokens) {
                if (skip) {
                    skip--;
                } else {
                    if (token.second == "::") {
                        std::string prev = out_tokens.back().second;
                        std::string next = tokens[pos+1].second;
                        out_tokens.push_back({"IDENT", prev + "::" + next});
                    } else {
                        out_tokens.push_back(token);
                    }
                    pos++;
                }
            }
            return out_tokens;
        }
        // ------

        std::function<Value*()> parse_expr(int min_prec=0) {
            auto [kind, val] = consume();

            if (kind == "OP" && pre_ops.count(val)) {
                Value* result = pre_ops[val]();
                return [result]() { return result; };
            }

            std::function<Value*()> left;

            if (kind == "NUMBER") {
                int num = std::stoi(val);
                left = [this, num]() {
                    return ConstantInt::get(types["int"], num);
                };
            } else if (kind == "STR") {
                left = [this, val]() { return _emit_string(val); };
            } else if (kind == "IDENT") {
                if (peek().second == "[") {
                    Value* gep = _parse_index(scope[val].first);
                    while (peek().second == "[") {
                        gep = _parse_index(gep);
                    }
                    left = [this, val, gep]() {
                        if (!scope.count(val)) {
                            error_msg("Usage of undeclared variable: " + val, pos);
                        }
                        return this->builder->CreateLoad(types["int"], gep);
                    };
                } else if (peek().second == "(") {
                    consume("LPAREN"); // (
                    std::vector<std::function<Value*()>> arg_fns;
                    while (peek().first != "RPAREN") {
                        arg_fns.push_back(parse_expr(0));
                        while (peek().first == "COMMA") {
                            consume(); // ,
                            arg_fns.push_back(parse_expr(0));
                        }
                    }
                    consume("RPAREN", "Expected ')' at the end of function call"); // )
                    left = [this, val, arg_fns]() {
                        if (!funcs.count(val)) error_msg("Call to unknown function " + val, pos);
                        std::vector<Value*> evaled_args;
                        for (auto& fn: arg_fns) {
                            evaled_args.push_back(fn());
                        }
                        return this->builder->CreateCall(funcs[val], evaled_args);
                    };
                } else {
                    left = [this, val]() { return _load_var(val); };
                }
            } else if (kind == "LPAREN") {
                left = parse_expr(0);
                consume("RPAREN", "Expected ')' at the end of expression"); // )
            } else {
                error_msg("Unexpected Token: " + val, pos);
            }
            while (peek().first == "OP") {
                std::string op_val = peek().second;
                if (!op_handlers.count(op_val)) break;
                int prec = op_handlers[op_val].first;
                OpHandler handler = op_handlers[op_val].second;
                if (prec < min_prec) break;

                consume(); // Op

                std::function<Value*()> right_fn = parse_expr(prec + 1);
                left = [this, left, right_fn, handler]() {
                    return handler(left(), right_fn());
                };
            }
            return left;
        }

        void parse_stmt() {
            auto [kind, val] = consume();

            if (kind == "OP" && val == "*") {
                Value* obj = parse_expr(100)();
                consume("OP");
                Value* val = parse_expr(0)();
                builder->CreateStore(val, obj);
            }

            if (kind == "IDENT" && types.count(val)) {
                Type* _type = types[val];
                _type = _parse_pointer(_type);

                std::string name = consume("IDENT").second;
                Value* ptr;

                if (peek().second == ";") {
                    // Prototype
                    ptr = builder->CreateAlloca(_type, nullptr, name);
                } else if (peek().second == "=") {
                    // Declaration
                    consume("OP"); // =
                    
                    Value* value = parse_expr(0)();

                    if (!builder) {
                        Constant* init_val = dyn_cast<Constant>(value);
                        ptr = new GlobalVariable(*module, _type, false, GlobalVariable::ExternalLinkage,
                        init_val ? init_val : Constant::getNullValue(_type), name);
                    } else {
                        ptr = builder->CreateAlloca(_type, nullptr, name);
                        builder->CreateStore(value, ptr);
                    }

                } else {
                    consume("LBRACKET"); // [
                    Value* index_val = parse_expr(0)();
                    consume("RBRACKET", "Expected ']' at the end of array declaration"); // ]

                    _type = ArrayType::get(_type, cast<ConstantInt>(index_val)->getZExtValue());
                    ptr = builder->CreateAlloca(_type, nullptr, name);
                }

                scope[name] = {ptr, _type, false};
            } else if (kind == "IDENT" && scope.count(val)) {
                if (peek().first == "LBRACKET") {
                    Value* item_ptr = _parse_index(scope[val].first);
                    while (peek().first == "LBRACKET") {
                        item_ptr = _parse_index(item_ptr);
                    }
                    consume("OP"); // =
                    Value* new_item = parse_expr(0)();
                    builder->CreateStore(new_item, item_ptr);
                } else {
                    consume("OP"); // =
                    Value* new_val = parse_expr(0)();
                    builder->CreateStore(new_val, scope[val].first);
                }
            } else if (kind == "KEYWORD" && keywords.count(val)) {
                keywords[val]();
            } else {
                if (!special_tokens.count(val)) {
                    pos--;
                    parse_expr(0)();
                }
            }
        }

        // Main function
        std::string compile(std::string code_text) {
            tokens = parse_namespaces(parse_tokens(split_text(code_text)));
            int t = 0;

            pos = 0;

            while(peek().first != "EOF") {
                parse_stmt();
            }

            std::string result;
            raw_string_ostream os(result);
            module->print(os, nullptr);
            return os.str();
        }

        // Other things...
        inline Value* _load_var(std::string name) {
            if (!scope.count(name)) error_msg("Usage of undeclared variable: '" + name + "'", pos);
            Type* _type = scope[name].second;
            Value* ptr = scope[name].first;
            bool is_local = scope[name].third;
            if (is_local) {
                return ptr;
            }
            return builder->CreateLoad(_type, ptr, name.c_str());
        }

        inline Value* _emit_string(std::string text) {
            std::vector<unsigned char> bytes(text.begin(), text.end());
            bytes.push_back(0);

            ArrayType* arr_type = ArrayType::get(types["int8"], bytes.size());

            std::mt19937 rng(std::random_device{}()); // we use random because... i dont know how to get hash of the string and dont add new unordered map for it
            std::uniform_int_distribution<long long> dist(0, 99999999);

            std::string var_name = ".str." + std::to_string(dist(rng));

            Constant* array_init = ConstantDataArray::get(module->getContext(), bytes);
            GlobalVariable* global_str = new GlobalVariable(
                *module, arr_type, true, GlobalValue::PrivateLinkage,
                array_init, var_name.c_str()
            );
            global_str->setConstant(true);
            Constant* zero = ConstantInt::get(Type::getInt32Ty(module->getContext()), 0);
            return builder->CreateGEP(arr_type, global_str, {zero, zero}, "str_ptr");
        }

        inline Value* _parse_index(Value* ptr_obj) {
            consume("LBRACKET"); // [
            Value* index = parse_expr(0)();
            consume("RBRACKET", "Expected ']' at the end of index"); // ]

            Constant* zero = ConstantInt::get(Type::getInt32Ty(module->getContext()), 0);
            return builder->CreateGEP(types["int"], ptr_obj, {index});
        }
        
        inline Type* _parse_pointer(Type* _type) {
            int ptrs = 0;
            while (peek().second == "*") {
                consume(); // *
                _type = PointerType::get(_type->getContext(), 0);
            }
            return _type;
        }
};

void run_file(std::string filename, Farx &compiler) {
    std::ifstream file(filename);

    if (file.is_open()) {
        std::ostringstream ss;
        ss << file.rdbuf();
        std::cout << compiler.compile(ss.str()) << std::endl;
    }
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        Farx compiler;
        for (int i = 1; i < argc; i++) {
            run_file(argv[i], compiler);
        }
        // verifyModule(*compiler.module, &llvm::errs()); good for debug
    } else {
        std::cout << "Usage: ./farx <your files>" << std::endl;
    }
    return 0;
}
