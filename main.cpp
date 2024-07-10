#include <cstdio>
#include <cstring>
#include <type_traits>
#include <variant>
#include <vector>

namespace op {

struct General;

struct Global {
	int type;
	int binding;

	enum {
		layout_in,
		layout_out
	} qualifier;
};

struct PrimitiveType {
	enum {
		i32,
		f32,
		vec4,
	} type;
};

struct Primitive {
	float fdata[4];
};

struct List {
	int item;
	int next;
};

struct Construct {
	int type;
	int args;
};

struct Store {
	int dst;
	int src;
};

using _general_base = std::variant <
	Global,
	PrimitiveType,
	Primitive,
	Construct,
	List,
	Store
>;

struct General : _general_base {
	using _general_base::_general_base;
};

struct _dump_dispatcher {
	op::General *pool;

	void operator()(const Global &global) {
		static const char *qualifier_table[] = {
			"layout input",
			"layout output"
		};

		printf("global: %%%d = (%s, %d)", global.type,
			qualifier_table[global.qualifier], global.binding);
	}

	void operator()(const op::PrimitiveType &type) {
		printf("type: ");
		switch (type.type) {
		case PrimitiveType::i32:
			printf("i32");
			break;
		case PrimitiveType::f32:
			printf("f32");
			break;
		case PrimitiveType::vec4:
			printf("vec4");
			break;
		default:
			printf("<?>");
			break;
		}
	}

	void operator()(const op::Primitive &p) {
		printf("primitive: (%.2f, %.2f, %.2f, %.2f)", p.fdata);
	}

	void operator()(const List &list) {
		printf("list: %%%d -> ", list.item);
		if (list.next >= 0)
			printf("%%%d", list.next);
		else
			printf("(nil)");
	}

	void operator()(const op::Construct &ctor) {
		printf("construct: %%%d = %%%d", ctor.type, ctor.args);
	}

	void operator()(const op::Store &store) {
		printf("store %%%d -> %%%d", store.src, store.dst);
	}

	template <typename T>
	void operator()(const T &) {
		printf("<?>");
	}
};

}

template <typename T>
concept synthesizable = requires {
	{ T().synthesize() } -> std::same_as <int>;
};

struct IREmitter {
	// By default the program begins at index=0
	op::General *pool;
	size_t size;
	size_t pointer;

	std::vector <int> main;

	IREmitter() : pool(nullptr), size(0), pointer(0) {}

	void reserve(size_t units) {
		if (pointer + units > size) {
			op::General *old = pool;

			size_t psize = size;
			size = pointer + units;
			pool = new op::General[size];

			if (old) {
				std::memcpy(pool, old, psize * sizeof(op::General));
				delete[] old;
			}
		}
	}

	int emit(const op::General &op) {
		if (pointer >= size) {
			printf("error, exceed global pool size, please reserve beforehand\n");
			return -1;
		}

		pool[pointer] = op;
		return pointer++;
	}

	int emit_main(const op::General &op) {
		int i = emit(op);
		main.push_back(i);
		return i;
	}

	void dump() {
		for (size_t i = 0; i < size; i++) {
			printf("[%4d]: ", i);
			std::visit(op::_dump_dispatcher { pool }, pool[i]);
			printf("\n");
		}
	}

	static IREmitter active;
};

IREmitter IREmitter::active;

struct tagged {
	int tag;

	tagged() {
		tag = next();
	}

	static int next() {
		static int local = 0;
		return local++;
	}
};

template <typename T>
struct gltype : tagged {
	T data;
	// bool cexpr;
	gltype() = default;
	gltype(T v) : data(v) {}
};

// TODO: fill this is in so that its not generated every time
struct layout_qualifier : tagged {
	// int synthesize() const {
	// 	auto &em = IREmitter::active;
	// }
};

template <typename T, size_t binding>
struct layout_in : gltype <T> {};

template <typename T, size_t binding>
requires synthesizable <T>
struct layout_out : gltype <T> {
	void operator=(const T &t) {
		auto &em = IREmitter::active;

		em.reserve(3);

		printf("op= from tag=%d\n", this->tag);
		// TODO: check if t already has an active iseq

		op::PrimitiveType type;
		type.type = op::PrimitiveType::vec4;

		op::Global qualifier;
		qualifier.type = em.emit(type);
		qualifier.binding = binding;
		qualifier.qualifier = op::Global::layout_out;

		op::Store store;
		store.dst = em.emit(qualifier);
		store.src = t.synthesize();

		em.emit_main(store);
	}
};

template <typename T, size_t N>
struct vec : gltype <T[N]> {
	constexpr vec() = default;

	constexpr vec(T v) {
		for (size_t i = 0; i < N; i++)
			this->data[i] = v;
	}

	int synthesize() const {
		// TODO: skip if already existing cache

		auto &em = IREmitter::active;

		em.reserve(1 + N);

		op::PrimitiveType t;
		t.type = op::PrimitiveType::vec4;

		op::Primitive p;
		// TODO: constexpr switch on T
		std::memcpy(p.fdata, this->data, N * sizeof(T));

		op::List l;
		l.item = em.emit(p);
		l.next = -1;

		op::Construct ctor;
		ctor.type = em.emit(t);
		ctor.args = em.emit(l);

		// TODO: emit and cache in one
		return em.emit_main(ctor);
	}
};

using vec4 = vec <float, 4>;

struct boolean : gltype <bool> {};

template <typename T>
boolean operator==(const gltype <T> &A, const gltype <T> &B)
{
	// get the tag of A and B and create a composite
	return {};
}

template <typename T, typename U>
requires std::is_constructible_v <gltype <T>, U>
boolean operator==(const gltype <T> &A, const U &B)
{
	return (A == gltype <T> (B));
}

// cond(...) for if
// elif(...) and elif() for else if and else
// loop(...) for while and auto v = loop(init, cond, step) for for
// end() to end all control flow

void fragment_shader()
{
	layout_in <int, 0> cond;
	layout_out <vec4, 0> fragment;

	fragment = vec4(1.0);
}

int main()
{
	fragment_shader();

	printf("IR:\n");
	IREmitter::active.dump();
}
