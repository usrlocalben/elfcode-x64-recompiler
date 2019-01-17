#include <algorithm>
#include <array>
#include <cassert>
#include <deque>
#include <iostream>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define NOMINMAX
#include <Windows.h>

using namespace std;

#define forn(i,n) for (int i=0; i<int(n); i++)
#define sz(a) (int((a).size()))
using VS = vector<string>;
using VI = vector<int>;


void split(const std::string& str, char ch, VS& out) {
	out.clear();
	std::string src(str);
	auto nextmatch = src.find(ch);
	while (1) {
		auto item = src.substr(0, nextmatch);
		out.push_back(item);
		if (nextmatch == std::string::npos) { break; }
		src = src.substr(nextmatch + 1);
		nextmatch = src.find(ch); }}

enum NativeOp { add, or, and, cmp, mov, xor };

enum Opcode {
	addr, addi,
	mulr, muli,
	banr, bani,
	borr, bori,
	setr, seti,
	gtir, gtri, gtrr,
	eqir, eqri, eqrr };


VS opcodeText = {
	"addr", "addi",
	"mulr", "muli",
	"banr", "bani",
	"borr", "bori",
	"setr", "seti",
	"gtir", "gtri", "gtrr",
	"eqir", "eqri", "eqrr" };


Opcode decodeOpcodeText(const string& text) {
	for (int i=0; i<opcodeText.size(); i++) {
		if (opcodeText[i] == text) {
			return static_cast<Opcode>(i); }}
	return Opcode::addr; }



extern "C" void _elfvm_run(int *reg, int *pc, uint8_t** code, int end);


class ElfVM {
public:
	ElfVM() {
		d_reg = new int[6];
		forn(i,6) d_reg[i] = 0;}
	~ElfVM() {
		if (d_code != nullptr) {
			VirtualFree(d_code, 0, MEM_RELEASE); }
		delete[] d_reg; }

	void setBreakpoint(int pc) {
		// mov eax, pc
		d_pclut[pc][0] = 0xb8;
		*reinterpret_cast<int*>(&(d_pclut[pc][1])) = pc;
		// ret
		d_pclut[pc][5] = 0xc3;}

	void run() {
		_elfvm_run(d_reg, &d_pc, d_pclut.data(), d_codeEnd); }

	void setRegister(int n, int value) { d_reg[n] = value; }
	auto getRegister(int n) const { return d_reg[n]; }

	void compile(const VS& lines) {
		VS segments;

		// the pc->native LUT can't have more entries than input program lines
		d_pclut.clear();
		d_pclut.resize(sz(lines), 0);

		// pass 1: compile to /dev/null, determine code size
		resetEmitter();
		d_cpc = 0;
		d_code = nullptr;
		for (const auto& line : lines) {
			//cout << ">>> " << line << "\n";
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				compileOne(segments);
				d_cpc++; }}
		emit_halt();  // halt if execution falls off the last instruction

		// pass 2: compile to exe memory, build pc->x86 addr table
		d_codeEnd = d_cpc;
		const auto nativeCodeSizeInBytes = d_epos;
		d_code = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, nativeCodeSizeInBytes, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		resetEmitter();
		d_cpc = 0;
		for (const auto& line : lines) {
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				d_pclut[d_cpc] = &d_code[d_epos];
				compileOne(segments);
				d_cpc++; }}
		emit_halt();

		// pass 3: compile final code
		resetEmitter();
		d_cpc = 0;
		for (const auto& line : lines) {
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				compileOne(segments);
				d_cpc++; }}
		emit_halt(); }

	void dump(const string& fn) {
		// ndisasm -b 64 code.bin
		ofstream fout(fn, ios::out | ios::binary);
		fout.write(reinterpret_cast<char*>(d_code), d_epos);
		fout.close(); }

private:
	void compileOne(const VS& segments) {
		// prefix with nop that can be used to set a breakpoint
		asm_nop6(); // room for "mov eax, 0x11223344; ret"

		const auto opcode = decodeOpcodeText(segments[0]);
		const int param1 = stoi(segments[1]);
		const int param2 = stoi(segments[2]);
		const int param3 = stoi(segments[3]);

		if (param3 == d_pcRegLink) {
			switch (opcode) {
			case Opcode::addr:
				if (param1 == d_pcRegLink && param1 == param2) {
					emit_jmp_hard(d_cpc + d_cpc + 1); }
				else {
					if (param1 == d_pcRegLink) {
						asm_lea_eax_reg32PlusImm32(param2+10, d_cpc+1); }
					else if (param2 == d_pcRegLink) {
						asm_lea_eax_reg32PlusImm32(param1+10, d_cpc+1); }
					else {
						// could be lea, but this combination will never happen anyway
						emit_load_register(param1);
						asm_inc_eax();
						emit_add_register(param2); }
					emit_jmp_soft();}
				break;
			case Opcode::addi:
				if (param1 == d_pcRegLink) {
					// relative jmp by constant
					emit_jmp_hard(d_cpc + 1 + param2);}
				else {
					// relative jmp by register
					emit_load_constant(d_cpc + 1);
					emit_add_register(param2);
					emit_jmp_soft(); }
				break;
			case Opcode::seti:
				// absolute jmp by constant
				emit_jmp_hard(param1 + 1);
				break;
			default:
				cout << "output=PC unsupported for instruction @" << d_cpc << "\n";
				exit(EXIT_FAILURE);}}

		else {
			switch (opcode) {
			case Opcode::addr:
				emit_op_reg_reg(NativeOp::add, param1, param2, param3);
				break;
			case Opcode::addi:
				emit_op_reg_imm(NativeOp::add, param1, param2, param3);
				break;
			case Opcode::mulr:
				emit_load_register(param1);
				asm_mov_edx_reg(param2);
				asm_imul_eax_edx();
				emit_store_register(param3);
				break;
			case Opcode::muli:
				if (param1 == param3) {
					if (param2 == 256) {
						asm_sal_r32_imm(param1+10, 8); }
					else {
						asm_imul_r32_r32_imm32(param1+10, param1+10, param2); }}
				else {
					emit_load_register(param1);
					emit_mul_constant(param2);
					emit_store_register(param3);}
				break;
			case Opcode::banr:
				emit_op_reg_reg(NativeOp::and, param1, param2, param3);
				break;
			case Opcode::bani:
				emit_op_reg_imm(NativeOp::and, param1, param2, param3);
				break;
			case Opcode::borr:
				emit_op_reg_reg(NativeOp::or, param1, param2, param3);
				break;
			case Opcode::bori:
				emit_op_reg_imm(NativeOp::or, param1, param2, param3);
				break;
			case Opcode::setr:
				asm_op_reg_reg(NativeOp::mov, 4, param3+10, param1+10);
				break;
			case Opcode::seti:
				if (param1 == 0) {
					asm_op_reg_reg(NativeOp::xor, 4, param3+10, param3+10); }
				else {
					asm_op_reg_imm(NativeOp::mov, 4, param3+10, param1);}
				break;
			case Opcode::gtir:
				//result = (arg1 > cpu.r[arg2] ? 1 : 0);
				asm_op_reg_imm(NativeOp::cmp, 4, param2+10, param1);
				asm_setle_r32(param3+10);
				break;
			case Opcode::gtri:
				//result = (cpu.r[arg1] > arg2 ? 1 : 0);
				asm_op_reg_imm(NativeOp::cmp, 4, param1+10, param1);
				asm_setg_r32(param3+10);
				break;
			case Opcode::gtrr:
				//result = (cpu.r[arg1] > cpu.r[arg2] ? 1 : 0);
				asm_op_reg_reg(NativeOp::cmp, 4, param1+10, param2+10);
				asm_setg_r32(param3+10);
				break;
			case Opcode::eqir:
				//result = (arg1 > cpu.r[arg2] ? 1 : 0);
				asm_op_reg_imm(NativeOp::cmp, 4, param2+10, param1);
				asm_sete_r32(param3+10);
				break;
			case Opcode::eqri:
				//result = (cpu.r[arg1] > arg2 ? 1 : 0);
				asm_op_reg_imm(NativeOp::cmp, 4, param1+10, param2);
				asm_sete_r32(param3+10);
				break;
			case Opcode::eqrr:
				//result = (cpu.r[arg1] > cpu.r[arg2] ? 1 : 0);
				asm_op_reg_reg(NativeOp::cmp, 4, param1+10, param2+10);
				asm_sete_r32(param3+10);
				break;
			default: assert(false); }}}

	// ElfCPU emitters
	void emit_halt() {
		// 5 bytes
		asm_xor_eax_eax();
		asm_dec_eax();
		asm_ret(); }

	void emit_jmp_soft() {
		// eax == destPC
		asm_cmp_eax_constant(d_codeEnd);
		asm_jl_rel8(5);  // willJmp

		emit_halt(); // halt cpu if OOB
//willJmp:
		// otherwise, jmp to native-code addr from pclut (r8)
		//asm_mov_eax_eax();
		asm_jmp_memAtR8PlusRAXTimes8();}

	void emit_jmp_hard(int destPC) {
		if (destPC < d_codeEnd) {
			asm_mov_rax_constant(reinterpret_cast<uint64_t>(d_pclut[destPC]));
			asm_jmp_rax(); }
		else {
			emit_halt(); }}

	void emit_op_reg_reg(NativeOp op, int param1, int param2, int param3) {
		assert(param3 != d_pcRegLink);
		if (param1 == param3 && param2 == param3) {
			// p3 = p3*2
			asm_op_reg_reg(op, 4, param3+10, param3+10);}
		else {
			if (param1 == param3) {
				asm_op_reg_reg(op, 4, param3+10, param2+10);}
			else if (param2 == param3) {
				asm_op_reg_reg(op, 4, param3+10, param1+10);}
			else {
				asm_op_reg_reg(NativeOp::mov, 4, param3+10, param1+10);
				asm_op_reg_reg(op, 4, param3+10, param2+10); }}}

	void emit_op_reg_imm(NativeOp op, int param1, int param2, int param3) {
		assert(param3 != d_pcRegLink);
		// XXX addi of 1 could be inc
		if (param1 == param3) {
			asm_op_reg_imm(op, 4, param3+10, param2); }
		else {
			asm_op_reg_imm(NativeOp::mov, 4, param3+10, param2);
			asm_op_reg_reg(op, 4, param3+10, param1+10); }}

	void emit_store_register(int n) {
		asm_movd_reg_eax(n+10); }

	void emit_load_register(int n) {
		if (n == d_pcRegLink) {
			emit_load_constant(d_cpc); }
		else {
			asm_movd_eax_reg(n+10); }}

	void emit_load_constant(int n) {
		if (n == 0) {
			asm_xor_eax_eax(); }
		else {
			asm_mov_eax_constant(n); }}

	void emit_add_register(int n) {
		if (n == d_pcRegLink) {
			emit_add_constant(d_cpc); }
		else {
			asm_add_eax_reg(n+10); }}

	void emit_add_constant(int value) {
		asm_add_eax_constant(value); }

	void emit_mul_constant(int value) {
		asm_imul_eax_eax_constant(value); }


	// x86-64 emitters
	void asm_op_reg_reg(const NativeOp op, int width, int r1, int r2) {
		//cout << ">>> " << r1 << ", " << r2 << "\n";
		assert(width==4);
		const int B = r1 < 8 ? 0 : 1;
		const int R = r2 < 8 ? 0 : 1;

		int prefix = -1;
		if (B && R) {
			prefix = 0x45; }
		else if (B) {
			prefix = 0x41; }
		else if (R) {
			prefix = 0x44; }

		if (prefix != -1) {
			db(prefix);}

		if (op == NativeOp::add) {
			db(0x01);}
		else if (op == NativeOp::and) {
			db(0x21);}
		else if (op == NativeOp::or) {
			db(0x09);}
		else if (op == NativeOp::cmp) {
			db(0x39);}
		else if (op == NativeOp::xor) {
			db(0x31);}
		else if (op == NativeOp::mov) {
			db(0x89);}
		else {
			cout << "unknown native opcode " << int(op) << "\n";
			exit(EXIT_FAILURE); }

		int operand = 0xc0; // mod = 11
		r1 = r1 % 8;
		r2 = r2 % 8;
		operand |= (r2 << 3);
		operand |= r1;
		//cout << hex << operand << dec << "\n";

		db(operand); }

	void asm_op_reg_imm(const NativeOp op, int width, int r, int imm) {
		//cout << ">>> " << r1 << ", " << r2 << "\n";
		assert(width==4);
		if (r < 8) {
			cout << "can't foo2 with reg<8\n";
			exit(EXIT_FAILURE);}
		r -= 8;

		db(0x41);
		if (op == NativeOp::mov) {
			db(0xb8 + r); }
		else {
			db(0x81);

			int operand = 0xc0;
			if (op == NativeOp::add) {
				operand |= 0; }   // 000b << 3
			else if (op == NativeOp::or) {
				operand |= 0x08; }// 001b << 3
			else if (op == NativeOp::and) {
				operand |= 0x20; }// 100b << 3
			else if (op == NativeOp::cmp) {
				operand |= 0x38; }// 111b << 3
			else {
				cout << "can't foo2 with opcode " << op << "\n";
				exit(EXIT_FAILURE); }

			operand |= r;
			//cout << hex << operand << dec << "\n";
			db(operand);}

		dd(imm); }

	void asm_lea_eax_reg32PlusImm32(int r, int imm) {
		assert(8 <= r && r <= 15);
		db(0x67);
		db(0x41);
		db(0x8d);
		db(0x80 + r-8);
		if (r == 12) {
			db(0x24); } // ???
		dd(imm); }

	void asm_movd_eax_reg(int n) {
		//  4489d0 = r10d
		//  4489d8 = r11d
		//  4489e0 = r12d
		assert(10 <= n && n <= 15);
		db(0x44);
		db(0x89);
		db((n<<3)|0x80);}

	void asm_add_eax_reg(int n) {
		assert(10 <= n && n <= 15);
		db(0x44);
		db(0x01);
		db((n<<3)|0x80);}

	void asm_add_eax_constant(int value) {
		db(0x05);
		dd(value); }

	void asm_mov_edx_reg(int n) {
		assert(0 <= n && n <= 15);
		const uint8_t codes[16] = {
			0xc2, 0xda, 0xca, 0xd2, 0xf2, 0xfa, 0xea, 0xe2,    // 89 nn
			0xc2, 0xca, 0xd2, 0xda, 0xe2, 0xea, 0xf2, 0xfa };  // 44 89 nn
		if (n >= 8) {
			db(0x44);}
		db(0x89);
		db(codes[n]); }

	void asm_sal_r32_imm(int r, int imm) {
		assert(8<=r && r<=15);
		db(0x41); // extended
		db(0xc1); // shift/rotate
		db(0xe0 + r - 8);  // sal + reg
		db(imm); }

	void asm_imul_eax_edx() {
		db(0x0f);
		db(0xaf);
		db(0xc2); }

	void asm_imul_eax_eax_constant(int value) {
		db(0x69);
		db(0xc0);
		dd(value); }

	void asm_movd_reg_eax(int n) {
		assert(10 <= n && n <= 15);
		db(0x41);
		db(0x89);
		db(0xb8 + n); }

	void asm_jmp_memAtR8PlusRAXTimes8() {
		// jmp [r8+rax*8]
		db(0x41);
		db(0xff);
		db(0x24);
		db(0xc0); }

	void asm_cmp_eax_constant(int c) {
		db(0x3d);
		dd(c); }

	void asm_jl_rel8(int c) {
		assert(c < 0x100);
		db(0x7c);
		db(uint8_t(c)); }

	void asm_setg_r32(int r) {
		asm_setcond_r32("g", r);}

	void asm_setle_r32(int r) {
		asm_setcond_r32("le", r);}

	void asm_sete_r32(int r) {
		asm_setcond_r32("e", r);}

	void asm_setcond_r32(const string& cond, int r) {
		assert(8 <= r && r <= 15);
		uint8_t code;
		if (cond == "g" || cond == "nle") {
			code = 0x9f; }
		else if (cond == "le" || cond == "ng") {
			code = 0x9e; }
		else if (cond == "e" || cond == "z") {
			code = 0x94; }
		else {
			cout << "unsupported setcond \"" << cond << "\" @" << d_cpc << "\n";
			exit(EXIT_FAILURE); }
		db(0x41);			// extended
		db(0x0f); db(code); // e.g "0f 9e" == setle
		db(0xc0 + r - 8);   // c0-c7 = r08-r15
		asm_movzx_r32_r8(r, r); }

	void asm_movzx_r32_r8(int r1, int r2) {
		assert(r1 == r2);
		db(0x45);
		db(0x0f); db(0xb6);	// movzx r16/32/64,r/m8
		array<uint8_t,8> code = { 0xc0, 0xc9, 0xd2, 0xdb, 0xe4, 0xed, 0xf6, 0xff };
		db(code[r1-8]); }

	void asm_imul_r32_r32_imm32(int r1, int r2, int imm) {
		assert(r1 == r2);
		array<uint8_t,8> code = { 0xc0, 0xc9, 0xd2, 0xdb, 0xe4, 0xed, 0xf6, 0xff };
		db(0x45); // extended
		db(0x69); // imul
		db(code[r1-8]); // operand
		dd(imm); }

	void asm_xor_eax_eax() {
		db(0x31);
		db(0xc0);}

	void asm_dec_eax() {
		db(0xff);
		db(0xc8);}

	void asm_ret() {
		db(0xc3);}

	void asm_nop1() {
		db(0x90);}

	void asm_nop6() {
		db(0x66);
		db(0x0f);
		db(0x1f);
		db(0x44);
		db(0x00);
		db(0x00);}

	void asm_inc_eax() {
		db(0xff);
		db(0xc0);}

	void asm_mov_eax_constant(int x) {
		db(0xb8);
		dd(x); }

	void asm_mov_rax_constant(uint64_t x) {
		db(0x48);
		db(0xb8);
		dq(x); }

	void asm_mov_eax_eax() {
		db(0x89);
		db(0xc0);}

	void asm_jmp_rax() {
		db(0xff);
		db(0xe0);}

	void db(uint8_t a) {
		if (d_code != nullptr) {
			d_code[d_epos] = a; }
		d_epos++;}

	void dw(uint16_t a) {
		if (d_code != nullptr) {
			*reinterpret_cast<uint16_t*>(&d_code[d_epos]) = a; }
		d_epos += 2;}

	void dd(uint32_t a) {
		if (d_code != nullptr) {
			*reinterpret_cast<uint32_t*>(&d_code[d_epos]) = a; }
		d_epos += 4;}

	void dq(uint64_t a) {
		if (d_code != nullptr) {
			*reinterpret_cast<uint64_t*>(&d_code[d_epos]) = a; }
		d_epos += 8;}

	void resetEmitter() {
		d_epos = 0;
		d_pcRegLink = -1; }

	uint8_t* d_code = nullptr;
	int d_epos = 0;			// codegen emitter position
	int d_cpc = 0;          // currently compiling PC
	int d_pc = 0;			// CPU Program Counter (in Elf instructions)
	int d_pcRegLink = -1;	// PC<->register link
	int d_codeEnd = 0;		// elfcode instruction count + 1
	int* d_reg;				// register file
	vector<uint8_t*> d_pclut; };


int main() {
	string line;
	VS prg;
	while (getline(cin, line)) {
		prg.push_back(line);}

	ElfVM vm;
	vm.compile(prg);
	//vm.dump("code.bin");
	vm.setBreakpoint(28);
	vm.setRegister(0, 0);

	unordered_set<int> seen;
	int prev = 0;
	int cur = 0;
	while (1) {
		//cout << "r" << flush;
		vm.run();
		//cout << "b" << flush;

		prev = cur;
		cur = vm.getRegister(1);
		if (seen.find(cur) != seen.end()) {
			break; }
		seen.insert(cur);}

	cout << prev << "\n";
	return 0; }
