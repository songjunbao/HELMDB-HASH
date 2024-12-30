在使用 Protocol Buffers（protobuf）3.x 版本时，遵循一些代码规范可以帮助保持代码的一致性和可读性。以下是一些常见的 protobuf 3 代码规范建议：

### 1. 文件命名和组织

- **文件命名**: 每个 `.proto` 文件应该有一个明确的命名，通常使用小写字母和下划线 `_` 分隔单词（例如 `my_service.proto`）。
- **文件结构**: 每个文件应该定义一个主要的消息类型，并且可以包含与该消息类型相关的其他辅助消息或服务定义。

### 2. 语法和版本

- **选择语法**: 优先选择使用 proto3 语法，除非你需要特定于 proto2 的功能。
- **版本声明**: 在文件的开头明确指定使用的 protobuf 版本，例如 `syntax = "proto3";`。

### 3. 消息和字段定义

- **消息命名**: 每个消息类型的名称应该是清晰和描述性的，使用驼峰命名法（例如 `MyMessage`）。
- **字段命名**: 每个字段名称应该清晰反映其含义，也应该使用驼峰命名法。
- **字段标识号**: 每个字段的标识号应该是唯一的，且应该按照从小到大的顺序分配。

### 4. 字段类型和默认值

- **字段类型**: 使用最适合数据类型的字段类型，如 `int32`, `uint64`, `bool`, `string`, `bytes`, `enum` 等。
- **默认值**: 如果字段有默认值，在消息定义中明确指定，默认值应该是清晰和合理的。

### 5. 枚举和服务定义

- **枚举**: 枚举类型应该用于表示一组有限的可能值，例如状态码、类型等。
- **服务**: 如果你的 `.proto` 文件定义了 RPC 服务，服务定义应该清晰描述每个 RPC 方法的输入和输出。

### 6. 注释和文档

- **注释**: 使用合适的注释（`//` 或 `/* */`）来解释每个消息、字段和服务定义的用途和约束。
- **文档生成**: 考虑在 `.proto` 文件中添加足够的注释，以便通过生成工具（如 `protoc` 的插件）生成文档。

### 7. 导入和依赖

- **导入其他文件**: 如果你的 `.proto` 文件依赖于其他 protobuf 文件，使用 `import` 声明依赖关系。
- **依赖管理**: 确保管理好 `.proto` 文件之间的依赖关系，避免循环依赖。

### 示例

以下是一个简单的示例，展示了如何应用这些规范：

```protobuf
syntax = "proto3";

// Import other proto files if needed
import "other.proto";

// Define a message
message MyMessage {
    int32 id = 1;  // Unique identifier
    string name = 2;  // Name of the entity
    repeated int32 values = 3;  // List of values
    MyEnumType type = 4;  // Enum type
}

// Define an enum type
enum MyEnumType {
    ENUM_TYPE_A = 0;
    ENUM_TYPE_B = 1;
}

// Define a service
service MyService {
    rpc GetMyData(MyRequest) returns (MyResponse);
}

// Define request and response messages for the service
message MyRequest {
    int32 request_id = 1;
}

message MyResponse {
    string response_data = 1;
}
```

通过遵循这些 protobuf 3 的代码规范，可以提高代码的可维护性和可读性，并确保与其它开发者协作时的一致性。