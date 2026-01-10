代码风格：
1. 4个空格缩进。一行不要超过120词
2. 函数和控制结构的左大括号 { 放在同一行的末尾，右大括号 } 单独占一行。
3. 函数名称为下划线，变量同。类名为大驼峰。类成员变量后端加下划线_。
4. 不使用异常。处理有异常的第三方库（例如cereal）需要try-catch。
5. 尽量使用cpp20标准库。



TODO:
1. 取消掉现在没有用到的unintialized逻辑，改为避免加载重复，考虑循环依赖是否能解决。
（uninitialized相当于promise）(DONE)

2. 还需要考虑Frames In Flight的问题，应该是有个结构持有每个frame的fence 。GPU同步

3. Prefab系统设计