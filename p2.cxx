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
		int pc;
		VS segments;

		// the pc->native LUT can't have more entries than input program lines
		d_pclut.clear();
		d_pclut.resize(sz(lines), 0);

		// pass 1: compile to /dev/null, determine code size
		resetEmitter();
		pc = 0;
		d_code = nullptr;
		for (const auto& line : lines) {
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				compileOne(pc++, segments); }}
		emit_halt();  // halt if execution falls off the last instruction

		// pass 2: compile to exe memory, build pc->x86 addr table
		d_codeEnd = pc;
		const auto nativeCodeSizeInBytes = d_epos;
		d_code = reinterpret_cast<uint8_t*>(VirtualAlloc(nullptr, nativeCodeSizeInBytes, MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		resetEmitter();
		pc = 0;
		for (const auto& line : lines) {
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				d_pclut[pc] = &d_code[d_epos];
				compileOne(pc++, segments); }}
		emit_halt();

		// pass 3: compile final code
		resetEmitter();
		pc = 0;
		for (const auto& line : lines) {
			split(line, ' ', segments);
			if (line[0] == '#') {
				d_pcRegLink = stoi(segments[1]); }
			else {
				compileOne(pc++, segments); }}
		emit_halt(); }

	void dump(const string& fn) {
		// ndisasm -b 64 code.bin
		ofstream fout(fn, ios::out | ios::binary);
		fout.write(reinterpret_cast<char*>(d_code), d_epos);
		fout.close(); }

private:
	void compileOne(const int pc, const VS& segments) {
		// prefix with nop that can be used to set a breakpoint
		asm_nop6(); // room for "mov eax, 0x11223344; ret"

		if (d_pcRegLink != -1) {
			emit_store_register_constant(d_pcRegLink, pc); }

		const auto opcode = decodeOpcodeText(segments[0]);
		const int param1 = stoi(segments[1]);
		const int param2 = stoi(segments[2]);
		const int param3 = stoi(segments[3]);

		switch (opcode) {
		case Opcode::addr:
			emit_load_register(param1);
			emit_add_register(param2);
			break;
		case Opcode::addi:
			emit_load_register(param1);
			emit_add_constant(param2);
			break;
		case Opcode::mulr:
			emit_load_register(param1);
			asm_mov_edx_reg(param2);
			asm_imul_eax_edx();
			break;
		case Opcode::muli:
			emit_load_register(param1);
			emit_mul_constant(param2);
			break;
		case Opcode::banr:
			emit_load_register(param1);
			emit_and_register(param2);
			break;
		case Opcode::bani:
			emit_load_register(param1);
			emit_and_constant(param2);
			break;
		case Opcode::borr:
			emit_load_register(param1);
			emit_or_register(param2);
			break;
		case Opcode::bori:
			emit_load_register(param1);
			emit_or_constant(param2);
			break;
		case Opcode::setr:
			emit_load_register(param1);
			break;
		case Opcode::seti:
			emit_load_constant(param1);
			break;
		case Opcode::gtir:
			//result = (arg1 > cpu.r[arg2] ? 1 : 0);
			emit_load_constant(param1);
			emit_cmp_register(param2);
			asm_setg_eax();
			break;
		case Opcode::gtri:
			//result = (cpu.r[arg1] > arg2 ? 1 : 0);
			emit_load_register(param1);
			emit_cmp_constant(param2);
			asm_setg_eax();
			break;
		case Opcode::gtrr:
			//result = (cpu.r[arg1] > cpu.r[arg2] ? 1 : 0);
			emit_load_register(param1);
			emit_cmp_register(param2);
			asm_setg_eax();
			break;
		case Opcode::eqir:
			//result = (arg1 == cpu.r[arg2] ? 1 : 0);
			emit_load_constant(param1);
			emit_cmp_register(param2);
			asm_sete_eax();
			break;
		case Opcode::eqri:
			//result = (cpu.r[arg1] == arg2 ? 1 : 0);
			emit_load_register(param1);
			emit_cmp_constant(param2);
			asm_sete_eax();
			break;
		case Opcode::eqrr:
			//result = (cpu.r[arg1] == cpu.r[arg2] ? 1 : 0);
			emit_load_register(param1);
			emit_cmp_register(param2);
			asm_sete_eax();
			break;
		default: assert(false); }

		emit_store_register(param3);

		if (d_pcRegLink == param3) {
			// output register is linked to PC
			if (opcode != Opcode::seti) {
				// general case, check for out of bounds
				asm_cmp_eax_constant(d_codeEnd);
				asm_jl_rel8(5);  // willJmp

				emit_halt(); // halt cpu if OOB
//willJmp:
				// otherwise, jmp to native-code addr from pclut (r8)
				asm_mov_eax_eax();
				asm_mov_rax_memAtR8PlusRAXTimes8Plus8();
				asm_jmp_rax(); }
			else {
				// seti to the pc-link register is either a jmp or halt
				if (param1 < d_codeEnd) {
					// precompute jmp
					asm_mov_rax_constant(reinterpret_cast<uint64_t>(d_pclut[param1 + 1]));
					asm_jmp_rax(); }
				else {
					emit_halt(); }}}}


	// ElfCPU emitters
	void emit_halt() {
		// 5 bytes
		asm_xor_eax_eax();
		asm_dec_eax();
		asm_ret(); }

	void emit_store_register_constant(int n, int value) {
		asm_movd_reg_constant(n+10, value); }

	void emit_store_register(int n) {
		asm_movd_reg_eax(n+10); }

	void emit_load_register(int n) {
		asm_movd_eax_reg(n+10); }

	void emit_load_constant(int n) {
		asm_mov_eax_constant(n); }

	void emit_add_register(int n) {
		asm_add_eax_reg(n+10); }

	void emit_add_constant(int value) {
		asm_add_eax_constant(value); }

	void emit_mul_constant(int value) {
		asm_imul_eax_eax_constant(value); }

	void emit_and_register(int n) {
		asm_and_eax_reg(n+10); }

	void emit_and_constant(int value) {
		asm_and_eax_constant(value); }

	void emit_or_register(int n) {
		asm_or_eax_reg(n+10); }

	void emit_or_constant(int value) {
		asm_or_eax_constant(value); }

	void emit_cmp_register(int n) {
		asm_cmp_eax_reg(n+10); }

	void emit_cmp_constant(int value) {
		asm_cmp_eax_constant(value); }


	// x86-64 emitters
	void asm_movd_reg_constant(int n, int value) {
		assert(10 <= n && n <= 15);
		db(0x41);
		db(0xb0 + n);  // expect ba->bf for r10d->r15d
		dw(value); }

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
		dw(value); }

	void asm_and_eax_reg(int n) {
		assert(10 <= n && n <= 15);
		db(0x44);
		db(0x21);
		db((n<<3)|0x80);}

	void asm_and_eax_constant(int value) {
		db(0x25);
		dw(value); }

	void asm_or_eax_reg(int n) {
		assert(10 <= n && n <= 15);
		db(0x44);
		db(0x09);
		db((n<<3)|0x80);}

	void asm_or_eax_constant(int value) {
		db(0x0d);
		dw(value); }

	void asm_mov_edx_reg(int n) {
		assert(0 <= n && n <= 15);
		const uint8_t codes[16] = {
			0xc2, 0xda, 0xca, 0xd2, 0xf2, 0xfa, 0xea, 0xe2,    // 89 nn
			0xc2, 0xca, 0xd2, 0xda, 0xe2, 0xea, 0xf2, 0xfa };  // 44 89 nn
		if (n >= 8) {
			db(0x44);}
		db(0x89);
		db(codes[n]); }

	void asm_imul_eax_edx() {
		db(0x0f);
		db(0xaf);
		db(0xc2); }

	void asm_imul_eax_eax_constant(int value) {
		db(0x69);
		db(0xc0);
		dw(value); }

	void asm_movd_reg_eax(int n) {
		assert(10 <= n && n <= 15);
		db(0x41);
		db(0x89);
		db(0xb8 + n); }

	void asm_mov_rax_memAtR8PlusRAXTimes8Plus8() {
		// mov rax, [r8+rax*8+8]
		// loads native code address of PC+1 for jmp
		db(0x49);
		db(0x8b);
		db(0x44);
		db(0xc0);
		db(0x08); }

	void asm_cmp_eax_constant(int c) {
		db(0x3d);
		dw(c); }

	void asm_cmp_eax_reg(int n) {
		assert(10<= n && n <= 15);
		db(0x44);
		db(0x39);
		db((n<<3)|0x80);}

	void asm_jl_rel8(int c) {
		assert(c < 0x100);
		db(0x7c);
		db(uint8_t(c)); }

	void asm_setg_eax() {
		db(0x0f); db(0x9f); db(0xc0);   // setg al
		db(0x0f); db(0xb6); db(0xc0);}  // movzx eax, al

	void asm_sete_eax() {
		db(0x0f); db(0x94); db(0xc0);   // setg al
		db(0x0f); db(0xb6); db(0xc0);}  // movzx eax, al

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

	void asm_shl_eax_constant(int c) {
		assert(c<32);
		db(0xc1);
		db(0xe0);
		db(uint8_t(c)); }

	void asm_mov_eax_constant(int x) {
		db(0xb8);
		dw(x); }

	void asm_mov_rax_constant(uint64_t x) {
		db(0x48);
		db(0xb8);
		dq(x); }

	void asm_mov_eax_eax() {
		db(0x89);
		db(0xc0);}

	void asm_mov_rax_ptrRDIPlusRAX() {
		db(0x48);
		db(0x8b);
		db(0x04);
		db(0x07);}

	void asm_jmp_rax() {
		db(0xff);
		db(0xe0);}

	void db(uint8_t a) {
		if (d_code != nullptr) {
			d_code[d_epos] = a; }
		d_epos++;}

	void dw(uint32_t a) {
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
