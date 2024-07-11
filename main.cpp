#include <cstdio>
#include <cstring>
#include <functional>
#include <type_traits>
#include <variant>
#include <stack>
#include <vector>
#include <cassert>

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
		boolean,
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

struct Cond {
	int cond;
	int failto;
};

struct Elif : Cond {};

struct End {};

using _general_base = std::variant <
	Global,
	PrimitiveType,
	Primitive,
	Construct,
	List,
	Store,
	Cond,
	Elif,
	End
>;

struct General : _general_base {
	using _general_base::_general_base;
};

struct _dump_dispatcher {
	void operator()(const Global &global) {
		static const char *qualifier_table[] = {
			"layout input",
			"layout output"
		};

		printf("global: %%%d = (%s, %d)", global.type,
			qualifier_table[global.qualifier], global.binding);
	}

	void operator()(const op::PrimitiveType &t) {
		static const char *type_table[] = {
			"bool", "int", "float", "vec4"
		};

		printf("type: %s", type_table[t.type]);
	}

	void operator()(const op::Primitive &p) {
		printf("primitive: (%.2f, %.2f, %.2f, %.2f)",
				p.fdata[0],
				p.fdata[1],
				p.fdata[2],
				p.fdata[3]);
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

	void operator()(const op::Cond &cond) {
		printf("cond %%%d -> %%%d", cond.cond, cond.failto);
	}

	void operator()(const op::Elif &elif) {
		if (elif.cond >= 0)
			printf("elif %%%d -> %%%d", elif.cond, elif.failto);
		else
			printf("elif (nil) -> %%%d", elif.failto);
	}

	void operator()(const End &) {
		printf("end");
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

	std::stack <int> control_flow_ends;

	IREmitter() : pool(nullptr), size(0), pointer(0) {}

	void resize(size_t units) {
		op::General *old = pool;

		pool = new op::General[units];
		if (old) {
			std::memcpy(pool, old, size * sizeof(op::General));
			delete[] old;
		}

		size = units;
	}

	int emit(const op::General &op) {
		if (pointer >= size) {
			if (size == 0)
				resize(1 << 4);
			else
				resize(size << 2);
		}

		if (pointer >= size) {
			printf("error, exceed global pool size, please reserve beforehand\n");
			throw -1;
			return -1;
		}

		pool[pointer] = op;
		return pointer++;
	}

	int emit(const op::Cond &cond) {
		int p = emit((op::General) cond);
		control_flow_ends.push(p);
		return p;
	}

	int emit(const op::Elif &elif) {
		int p = emit((op::General) elif);
		assert(control_flow_ends.size());
		auto ref = control_flow_ends.top();
		control_flow_ends.pop();
		control_flow_callback(ref, p);
		control_flow_ends.push(p);
		return p;
	}

	int emit(const op::End &end) {
		int p = emit((op::General) end);
		assert(control_flow_ends.size());
		auto ref = control_flow_ends.top();
		control_flow_ends.pop();
		control_flow_callback(ref, p);
		return p;
	}

	void control_flow_callback(int ref, int p) {
		auto &op = pool[ref];
		if (std::holds_alternative <op::Cond> (op)) {
			std::get <op::Cond> (op).failto = p;
		} else if (std::holds_alternative <op::Elif> (op)) {
			std::get <op::Elif> (op).failto = p;
		} else {
			printf("op not conditional, is actually:");
			std::visit(op::_dump_dispatcher(), op);
			printf("\n");
			assert(false);
		}
	}

	void dump() {
		printf("GLOBALS (%4d/%4d)\n", pointer, size);
		for (size_t i = 0; i < pointer; i++) {
			printf("[%4d]: ", i);
			std::visit(op::_dump_dispatcher(), pool[i]);
			printf("\n");
		}
	}

	// TODO: one per thread
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

		em.emit(store);
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
		return em.emit(ctor);
	}
};

using vec4 = vec <float, 4>;

struct boolean : gltype <bool> {
	int synthesize() {
		auto &em = IREmitter::active;

		op::PrimitiveType t;
		t.type = op::PrimitiveType::f32;

		return em.emit(t);
	}
};

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

// Branching emitters
void cond(boolean b)
{
	auto &em = IREmitter::active;
	op::Cond branch;
	branch.cond = b.synthesize();
	em.emit(branch);
}

void elif(boolean b)
{
	auto &em = IREmitter::active;
	op::Elif branch;
	branch.cond = b.synthesize();
	em.emit(branch);
}

void elif()
{
	// Treated as an else
	auto &em = IREmitter::active;
	op::Elif branch;
	branch.cond = -1;
	em.emit(branch);
}

void end()
{
	auto &em = IREmitter::active;
	em.emit(op::End());
}

void fragment_shader()
{
	layout_in <int, 0> flag;
	layout_out <vec4, 0> fragment;

	cond(flag == 0);
		fragment = vec4(1.0);
	elif(flag == 1);
		fragment = vec4(0.5);
	elif();
		fragment = vec4(0.1);
	end();
}

int main()
{
	fragment_shader();

	printf("IR:\n");
	IREmitter::active.dump();
}
