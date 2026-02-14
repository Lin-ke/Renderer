遵循如下工作流：
1. 遵循如下编码风格完成任务。禁止使用占位符如注释,dummy code,default，// ... 这种内容偷懒！
如果逻辑实在复杂，你认为需要跳过，必须使用//####TODO####注释。尽可能少省略。如果你省略，必须说明理由。
如果你需要阅读代码，首先阅读文档。目前还没有。
2. 所有任务完成后需要测试！ 在test/目录下编写测试代码，并简单命名测试任务。
3. 调用xmake run utest ["tag"] 而非xmake build，并循环直到任务完成。tag的内容在test要求中。
4. 在最后总结时，按照如下结构：
    1. 说明你做了什么：“I have”
    2. 说明你省略了什么：“I omitted”，特别是TODO注释的部分。
    3. 尽可能少省略。如果你省略，必须说明理由。

代码风格：
1. 4个空格缩进。一行不要超过120词
2. 函数和控制结构的左大括号 { 放在同一行的末尾，右大括号 } 单独占一行。
3. 函数名称为下划线，变量同。类名为大驼峰。类成员变量后端加下划线_。
4. 不使用异常。处理有异常的第三方库（例如cereal）需要try-catch。
5. 尽量使用cpp20标准库。
6. API需要加入Doxgen style注释

7. 日志风格：
DEFINE_LOG_TAG(LogTAG, "TAGName")。
以及INFO(LogTAG, "Message: {}", variable);

8. 失败风格：不使用异常。会失败的函数使用std::optional<T>作为返回值，失败时返回std::nullopt。调用者需要检查返回值是否有效，并使用ERR print和返回表示失败。


test的要求：目录使用test/，资源位置放在test/test_internal。不要使用Section！
已有的test（如果你加入test，需要补充在下面）：文件名为"test"+"_"+tag
dx11_triangle
prefab
rdg
reflection
scene
thread_pool
render_resource
shader


Before answering, review the guidelines in README.md strictly.
回答前，严格回顾 README.md 中的准则。