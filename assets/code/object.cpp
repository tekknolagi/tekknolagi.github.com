#include <cstdint>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <vector>

class Header {
  public:
  // Index into shape table
  uint32_t shape_id;
  // Size of the object in words, excluding the header. So the real size is
  // num_slots+1. This is fixed at allocation time.
  uint32_t num_slots;
};

const size_t kWordSize = 8;
const size_t kDefaultObjectNumSlots = 2;

class Object {
  public:
  explicit Object(Header header) {
    header_ = header;
    std::memset(attributes(), 0, header.num_slots * kWordSize);
  }

  static Object *allocate(Header header) {
    size_t num_slots = std::max((size_t)header.num_slots, kDefaultObjectNumSlots);
    void *result_raw = ::operator new((num_slots+1) * kWordSize);
    return new (result_raw) Object(header);
  }

  int64_t integer_value() {
    return reinterpret_cast<const int64_t>(attributes()[0]);
  }

  Object **attributes() { return reinterpret_cast<Object**>(this + 1); }

  Header header_;
};

enum class TransitionKind {
  kAddSlot,
  kRemoveSlot,  // For example. Not implemented in this demo.
};

class Shape;

struct Transition {
  TransitionKind kind;
  std::string name;
  Shape *target_shape;
};

class Attribute {
  public:
  explicit Attribute(const std::string& name) : name(name) {}

  std::string name;
  // Could have type information here.
};

class Shape {
  public:
  explicit Shape(uint32_t id) : id(id) {}

  uint32_t num_attributes() const { return attributes.size(); } 

  uint32_t id;
  std::vector<Attribute> attributes;
  std::vector<Transition> transitions;
};

class Runtime {
  public:
  explicit Runtime() {
    empty_shape_ = new_shape_with_attributes({});
    integer_shape_ = new_shape_with_attributes(std::vector<Attribute>{Attribute{"value"}});
  }

  Shape *shape(uint32_t shape_id) { return shapes_.at(shape_id); }

  Shape *new_shape_with_attributes(const std::vector<Attribute>& attributes) {
    uint32_t id = shapes_.size();
    Shape *shape = new Shape(id);
    shape->attributes = attributes;
    shapes_.push_back(shape);
    return shape;
  }

  const Shape *shape_transition(Shape *shape, TransitionKind kind, const std::string& name) {
    for (const auto& transition : shape->transitions) {
      if (transition.kind == kind && transition.name == name) {
        return transition.target_shape;
      }
    }
    // TODO(max): Handle attribute removal
    Shape *result = new_shape_with_attributes(shape->attributes);
    result->attributes.push_back(Attribute{name});
    shape->transitions.push_back(Transition{kind, name, result});
    return result;
  }

  Object *new_object(uint32_t num_slots) {
    Header header;
    header.shape_id = empty_shape_->id;
    header.num_slots = num_slots;
    return Object::allocate(header);
  }

  // Most runtimes have some kind of pointer tagging instead of storing small
  // integers in heap-allocated containers
  Object *new_integer(int64_t value) {
    Header header;
    header.shape_id = integer_shape_->id;
    header.num_slots = 1;
    Object *result = Object::allocate(header);
    std::memcpy(result->attributes(), &value, sizeof(value));
    return result;
  }

  Object *read_attr(Object *obj, const std::string& name) {
    const Shape *shape = this->shape(obj->header_.shape_id);
    for (uint32_t i = 0; i < shape->num_attributes(); ++i) {
      if (shape->attributes[i].name == name) {
        return obj->attributes()[i];
      }
    }
    std::fprintf(stderr, "Attribute %s not found in shape %u\n", name.c_str(), shape->id);
    std::abort();
    return nullptr;
  }

  Object *write_attr(Object *obj, const std::string& name, Object *value) {
    Shape *shape = this->shape(obj->header_.shape_id);
    for (uint32_t i = 0; i < shape->num_attributes(); ++i) {
      if (shape->attributes[i].name == name) {
        obj->attributes()[i] = value;
        return obj;
      }
    }
    if (shape->num_attributes() >= obj->header_.num_slots) {
      // TODO(max): Handle attribute overflow with overflow storage slot
      std::fprintf(stderr, "Cannot add attribute %s to object with shape %u: no more slots\n", name.c_str(), shape->id);
      std::abort();
    }
    size_t next_index = shape->num_attributes();
    const Shape *new_shape = shape_transition(shape, TransitionKind::kAddSlot, name);
    obj->header_.shape_id = new_shape->id;
    obj->attributes()[next_index] = value;
    return obj;
  }

  private:
  std::vector<Shape *> shapes_;
  Shape *empty_shape_ = nullptr;
  Shape *integer_shape_ = nullptr;
};

int main() {
  Runtime runtime;
  Object *obj = runtime.new_object(4);
  Object *three = runtime.new_integer(3);
  std::fprintf(stdout, "Integer value: %" PRId64 "\n", three->integer_value());
  Object *four = runtime.new_integer(4);
  std::fprintf(stdout, "Integer value: %" PRId64 "\n", four->integer_value());
  runtime.write_attr(obj, "x", three);
  runtime.write_attr(obj, "y", four);
  std::fprintf(stdout, "Attribute x: %" PRId64 "\n", runtime.read_attr(obj, "x")->integer_value());
  std::fprintf(stdout, "Attribute y: %" PRId64 "\n", runtime.read_attr(obj, "y")->integer_value());
  return 0;
}
