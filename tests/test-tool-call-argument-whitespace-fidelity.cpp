#include "chat-peg-parser.h"
#include "chat.h"
#include "common.h"
#include "peg-parser.h"
#include "testing.h"
#include "nlohmann/json.hpp"

#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::ordered_json;

int main(int argc, char * argv[]) {
    testing t(std::cout);

    t.test("whitespace preservation in tool arguments", [&](testing & t) {
        json tools = json::array();
        json tool_test = {
            { "type",     "function" },
            { "function",
             {
                  { "name", "test_tool" },
                  { "description", "A test tool" },
                  { "parameters",
                    {
                        { "type", "object" },
                        { "properties",
                          { { "arg1",
                              { { "type", "string" }, { "description", "The arg." } } } } },
                        { "required", { "arg1" } },
                    }
                }
            }                       }
        };
        tools.push_back(tool_test);

        auto parser = build_chat_peg_parser([&](common_chat_peg_builder & p) {
            // Standard JSON tools parser.
            // Note: standard_json_tools often expects the tool calls to be inside a JSON array
            // if parallel_tool_calls is true, but here we test a single call.
            auto tool_call = p.standard_json_tools("<tool_call>[", "]</tool_call>", tools, false, false);
            return p.sequence({ p.content(p.until("<tool_call>")), p.optional(p.space() + tool_call), p.space(), p.end() });
        });

        struct TestCase {
            std::string name;
            std::string input;
            std::string expected_arg_value;
        };

        std::vector<TestCase> test_cases = {
            {
                "leading/trailing spaces",
                R"arg(
                <tool_call>[{
                    "name": "test_tool", 
                    "arguments": {"arg1": "  spaced  "}
                }]</tool_call>
                )arg",
                "  spaced  "
            },
            {
                "tabs",
                R"arg(
                <tool_call>[{
                    "name": "test_tool", 
                    "arguments": {"arg1": "\ttabbed\t"}
                }]</tool_call>
                )arg",
                "\ttabbed\t"
            },
            {
                "internal whitespace",
                R"arg(
                <tool_call>[{
                    "name": "test_tool", 
                    "arguments": {"arg1": "multiple   spaces   and\t\ttabs"}
                }]</tool_call>
                )arg",
                "multiple   spaces   and\t\ttabs"
            },
            {
                "multiline with mixed whitespace",
                R"arg(
                <tool_call>[{
                    "name": "test_tool", 
                    "arguments": {"arg1": "\n  line1\n\tline2\n  "}
                }]</tool_call>
                )arg",
                "\n  line1\n\tline2\n  "
            },
            {
                "extreme mixed whitespace",
                R"arg(
                <tool_call>[{
                    "name": "test_tool",
                    "arguments": {"arg1": " \t \n \t "}
                }]</tool_call>
                )arg",
                " \t \n \t "
            }
        };

        for (const auto & tc : test_cases) {
            t.test(tc.name, [&](testing & t) {
                common_peg_parse_context ctx(tc.input);
                auto result = parser.parse(ctx);

                t.assert_true("parsing success", result.success());
//                t.assert_equal("number of tool calls", 1u, result.tool_calls.size());

                common_chat_msg msg;
                auto            mapper = common_chat_peg_mapper(msg);
                mapper.from_ast(ctx.ast, result);

                t.assert_equal("number of tool calls in msg", 1u, msg.tool_calls.size());

                // Parse the arguments string as JSON to inspect the actual value
                json parsed_args = json::parse(msg.tool_calls[0].arguments);
                t.assert_equal("arg1 value preserves all whitespace", tc.expected_arg_value, parsed_args["arg1"].get<std::string>());
            });
        }
    });

    return t.summary();
}
