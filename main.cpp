#include <iostream>
#include <fstream>
using namespace std;

constexpr auto N = 128 * 1024;
constexpr auto Nbit = 17; // log2(N);

class Replacer {
public:
	virtual void visit(int n) = 0; // do something to recently visited n
	virtual int get_and_visit_victim() = 0; // get and visit victim
	virtual ~Replacer() = default;
};

class LRU :public Replacer {
	int size_; // size must be power of 2
	int size_bit_;
	int nc_;
	unsigned char* stack_;

	int get(const int n) const { // get the n-th line of the stack
		int s = n * size_bit_, e = s + size_bit_ - 1;
		int sc = s >> 3, ec = e >> 3;
		int so = 8 - s % 8, eo = 7 - e % 8;
		if (sc == ec) {
			return (stack_[sc] % (1 << so)) >> eo;
		}
		else {
			int a = stack_[sc] % (1 << so);
			for (auto i = 1, mid = ec - sc; i < mid; ++i) {
				a = (a << 8) + stack_[sc + i];
			}
			a = (a << (8 - eo)) + (stack_[ec] >> eo);
			return a;
		}
	}
	void set(const int n, int x) const { // set the n-th line of the stack
		int s = n * size_bit_, e = s + size_bit_ - 1;
		int sc = s >> 3, ec = e >> 3;
		int so = 8 - s % 8, eo = 7 - e % 8;
		if (sc == ec) {
			auto a = stack_[sc];
			a &= ~((size_ - 1) << eo);
			a |= x << eo;
			stack_[sc] = a;
		}
		else {
			auto a = stack_[ec];
			a &= ~(255 << eo);
			a |= (x % (1 << (8 - eo))) << eo;
			stack_[ec] = a;
			x >>= 8 - eo;
			for (auto i = ec - 1; i > sc; --i) {
				stack_[i] = x % 256;
				x >>= 8;
			}
			a = stack_[sc];
			a &= ~((1 << so) - 1);
			a |= x;
			stack_[sc] = a;
		}
	}
	void move_to_top(const int n) { // move the n-th line to the top of the stack
		if (n == 0) {
			return;
		}
		auto x = get(n);
		for (auto i = n; i > 0; --i) {
			set(i, get(i - 1));
		}
		set(0, x);
	}

public:
	LRU(const int size) :size_(size) {
		size_bit_ = 0;
		while ((1 << size_bit_) < size_) {
			++size_bit_;
		}
		nc_ = (size_bit_ * size_ + 7) >> 3;
		stack_ = new unsigned char[nc_];
		for (auto i = 0; i < size_; ++i) {
			set(i, i);
		}
	}

	void visit(const int x) override { // push a number into the stack
		for (auto i = 0; i < size_; ++i) {
			if (get(i) == x) {
				move_to_top(i);
				break;
			}
		}
	}

	int get_and_visit_victim() override {
		move_to_top(size_ - 1);
		return get(0);
	}

	~LRU() {
		delete[] stack_;
	}
	//void show() {
	//	int cnt = 0;
	//	for (auto i = 0; i < 8; ++i) {
	//		for (auto j = 7; j >= 0; --j) {
	//			if (cnt % size_bit_ == 0) {
	//				cout << "|";
	//			}
	//			cout << (stack_[i] >> j) % 2;
	//			++cnt;
	//		}
	//		cout << " ";
	//	}
	//	cout << endl;
	//}
};

class Random :public Replacer {
	int size_;
public:
	Random(int size) :size_(size) {}
	void visit(int n) override {}
	int get_and_visit_victim() override {
		return rand() % size_; // size不能超过RAND_MAX
	}
};

class BT :public Replacer {
	int size_;
	int nc_;
	int h_; // height of tree
	unsigned char* tree_;
	bool get(const int n) const {
		return tree_[n >> 3] & (1 << (n % 8));
	}
	void set(const int n, const bool x) {
		if (x) {
			tree_[n >> 3] |= 1 << (n % 8);
		}
		else {
			tree_[n >> 3] &= ~(1 << (n % 8));
		}
	}
public:
	BT(int size) :size_(size) { // size must be power of 2
		nc_ = (size_ + 6) >> 3; // only (size_-1) bits needed
		tree_ = new unsigned char[nc_];
		memset(tree_, 0, nc_);
		h_ = 0;
		while ((1 << ++h_) < size_);
	}
	void visit(int n) override {
		n += size_ - 1; // 修正n为二叉树叶子节点编号
		int p;
		do {
			p = (n - 1) >> 1;
			set(p, n % 2); // 奇数在上，偶数在下
			n = p;
		} while (p > 0);
	}
	int get_and_visit_victim() override {
		int victim = 0, n = 0;
		for (auto i = 0; i < h_; ++i) {
			auto m = get(n);
			victim = (victim << 1) + m;
			n = (n << 1) + m + 1;
		}
		visit(victim);
		return victim;
	}
	~BT() {
		delete[] tree_;
	}
};

class Cache {
	char mapping_algo_;// 映射规则(直接映射，全关联，4-way组关联，8-way组关联)
	char replacing_algo_;// 替换算法(LRU，随机替换，二叉树替换)
	char writing_algo_;// 写策略(写不分配+写直达，写分配+写回，写不分配+写回，写分配+写直达)

	bool has_dirty_; // whether there's a dirty bit
	bool write_alloc_; // 是否写分配
	int tag_len_, index_len_, offset_len_, length_; // length of each field and total length
	int nc_; // # of chars for each cache line

	unsigned char** data_; // cache lines
	int n_; // # of cache lines
	int last_visit_; // 上一次访问位置，用于全相联查找

	Replacer** replacer_; // array of replacers


public:
	Cache(const char block_size = 8, const char mapping_algo = 3, const char replacing_algo = 0, const char writing_algo = 1) : mapping_algo_(mapping_algo), replacing_algo_(replacing_algo), writing_algo_(writing_algo) {
		offset_len_ = 0;
		while ((1 << offset_len_) < block_size) {
			++offset_len_;
		}

		index_len_ = Nbit - offset_len_;
		switch (mapping_algo_) {
		case 0:break;
		case 1:index_len_ = 0; break;
		case 2:index_len_ -= 2; break;
		default:index_len_ -= 3; break;
		}

		write_alloc_ = (writing_algo_ % 2 == 1);

		tag_len_ = 64 - index_len_ - offset_len_;
		length_ = tag_len_ + 1;
		has_dirty_ = (writing_algo_ == 1 || writing_algo_ == 2);
		if (has_dirty_) {
			length_ += 1;
		}
		nc_ = (length_ + 7) >> 3;
		n_ = N >> offset_len_;

		// 初始化cache line
		data_ = new unsigned char* [n_];
		for (auto i = 0; i < n_; ++i) {
			data_[i] = new unsigned char[nc_];
			memset(data_[i], 0, nc_);
		}

		last_visit_ = 0;

		int j;
		auto ways = 4, ways_bit = 2;
		switch (mapping_algo_) {
		case 0: //直接映射，不需要替换算法
			replacer_ = nullptr;
			break;
		case 1: //全相联，只有一个LRU
			replacer_ = new Replacer*;
			switch (replacing_algo_) {
			case 0:*replacer_ = new LRU(n_); break;
			case 1:*replacer_ = new Random(n_); break;
			default:*replacer_ = new BT(n_); break;
			}
			break;
		default: // 4-way or 8-way
			if (mapping_algo_ == 3) { // 8-way
				ways = 8;
				ways_bit = 3;
			}
			j = n_ >> ways_bit;
			replacer_ = new Replacer * [j];
			for (auto i = 0; i < j; ++i) {
				switch (replacing_algo_) {
				case 0:replacer_[i] = new LRU(ways); break;
				case 1:replacer_[i] = new Random(ways); break;
				default:replacer_[i] = new BT(ways); break;
				}
			}
			break;
		}
	}

	bool read(const uint64_t addr) {
		bool hit = false;
		unsigned char* line;
		int victim = -1, ways = 4, ways_bit = 2;
		int index = (addr % (uint64_t(1) << (offset_len_ + index_len_))) >> offset_len_;
		auto tag = addr >> (index_len_ + offset_len_);
		switch (mapping_algo_) {
		case 0: // 直接映射
			line = data_[index];
			if ((line[0] & 1) && tag_eq(line, tag)) {
				hit = true;
			}
			else {
				load(index, tag);
			}
			break;
		case 1: // 全相联
			for (auto i = last_visit_, j = i + n_; i < j; ++i) {
				auto ind = i % n_;
				line = data_[ind];
				if (line[0] & 1) {
					if (tag_eq(line, tag)) {
						hit = true;
						last_visit_ = ind;
						replacer_[0]->visit(last_visit_);
						break;
					}
				}
				else if (victim < 0) { // 寻找第一个可插入位置
					victim = ind;
				}
			}
			if (!hit) {
				if (victim < 0) { // 如果没有可插入位置，需要替换
					victim = replacer_[0]->get_and_visit_victim();
				}
				else {
					replacer_[0]->visit(victim);
				}
				load(victim, tag);
			}
			break;
		default: // 4-way or 8-way
			if (mapping_algo_ == 3) { // 8-way
				ways = 8;
				ways_bit = 3;
			}
			auto ind = index << ways_bit;
			victim = -1;
			for (auto i = 0; i < ways; ++i) {
				line = data_[ind + i];
				if (line[0] & 1) {
					if (tag_eq(line, tag)) {
						hit = true;
						replacer_[index]->visit(i);
						break;
					}
				}
				else {
					if (victim < 0) {
						victim = i;
					}
				}
			}
			if (!hit) {
				if (victim < 0) {
					victim = replacer_[index]->get_and_visit_victim();
				}
				else {
					replacer_[index]->visit(victim);
				}
				load(ind + victim, tag);
				//cout << victim << endl;
			}
			break;

		}
		//cout << "read " << addr << endl;
		return hit;
	}

	bool write(const uint64_t addr) {
		bool hit = false;
		unsigned char* line;
		int victim = -1, ways = 4, ways_bit = 2;
		int index = (addr % (uint64_t(1) << (offset_len_ + index_len_))) >> offset_len_;
		auto tag = addr >> (index_len_ + offset_len_);
		switch (mapping_algo_) {
		case 0: // 直接映射
			line = data_[index];
			if ((line[0] & 1) && tag_eq(line, tag)) {
				hit = true;
				if (has_dirty_) {
					line[0] |= 2;
				}
			}
			else if (write_alloc_) {
				load(index, tag);
				if (has_dirty_) {
					data_[index][0] |= 2;
				}
			}
			break;
		case 1: // 全相联
			for (auto i = last_visit_, j = i + n_; i < j; ++i) {
				auto ind = i % n_;
				line = data_[ind];
				if (line[0] & 1) {
					if (tag_eq(line, tag)) {
						hit = true;
						if (has_dirty_) {
							line[0] |= 2;
						}
						last_visit_ = ind;
						replacer_[0]->visit(last_visit_);
						break;
					}
				}
				else if (victim < 0) { // 寻找第一个可插入位置
					victim = ind;
				}
			}
			if (!hit && write_alloc_) {
				if (victim < 0) { // 如果没有可插入位置，需要替换
					victim = replacer_[0]->get_and_visit_victim();
				}
				else {
					replacer_[0]->visit(victim);
				}
				load(victim, tag);
				if (has_dirty_) {
					data_[victim][0] |= 2;
				}
			}
			break;
		default:
			// 4-way
			if (mapping_algo_ == 3) {// 8-way
				ways = 8;
				ways_bit = 3;
			}
			auto ind = index << ways_bit;
			victim = -1;
			for (auto i = 0; i < ways; ++i) {
				line = data_[ind + i];
				if (line[0] & 1) {
					if (tag_eq(line, tag)) {
						hit = true;
						if (has_dirty_) {
							line[0] |= 2;
						}
						replacer_[index]->visit(i);
						break;
					}
				}
				else {
					if (victim < 0) {
						victim = i;
					}
				}
			}
			if (!hit && write_alloc_) {
				if (victim < 0) {
					victim = replacer_[index]->get_and_visit_victim();
				}
				else {
					replacer_[index]->visit(victim);
				}
				load(ind + victim, tag);
				if (has_dirty_) {
					data_[ind + victim][0] |= 2;
				}
			}
			break;
		}
		//cout << "write " << addr << endl;
		return hit;
	}

	bool tag_eq(const unsigned char* line, uint64_t tag) const {
		/*uint64_t t = 0;
		for (auto i = nc_ - 1; i > 0; --i) {
			t = (t << 8) + line[i];
		}
		if (has_dirty_) {
			t = (t << 6) + (line[0] >> 2);
		}
		else {
			t = (t << 7) + (line[0] >> 1);
		}
		return t == tag;*/
		if (has_dirty_) {
			if (tag % 64 != line[0] >> 2) {
				return false;
			}
			tag >>= 6;
		}
		else {
			if (tag % 128 != line[0] >> 1) {
				return false;
			}
			tag >>= 7;
		}
		for (auto i = 1; i < nc_; ++i) {
			if (tag % 256 != line[i]) {
				return false;
			}
			tag >>= 8;
		}
		return true;
	}

	void load(int index, uint64_t tag) { // load a block into cache
		auto d = data_[index];
		d[0] = 0;
		if (has_dirty_) {
			d[0] = (tag % 64) << 2;
			tag >>= 6;
		}
		else {
			d[0] = (tag % 128) << 1;
			tag >>= 7;
		}
		d[0] |= 1;
		for (auto i = 1; i < nc_; ++i) {
			d[i] = tag % 256;
			tag >>= 8;
		}
	}

	~Cache() {
		for (auto i = 0; i < n_; ++i) {
			delete data_[i];
		}
		delete data_;
	}
};

uint64_t hextoi(const char* str) { // convert base-16 to base-10
	uint64_t num = 0;
	char a;
	for (auto i = 2;; i++) {
		a = str[i];
		if (a) {
			if (a - 'a' >= 0) {
				a += 10 - 'a';
			}
			else {
				a -= '0';
			}
			num = (num << 4) + a;
		}
		else {
			return num;
		}
	}
}

#if false
int main() {
	srand(0); // 保证实验结果的可重复性
	auto path = "C:\\Users\\johna\\Data\\astar.trace";
	ifstream trace_file;
	trace_file.open(path);
	if (!trace_file.is_open()) {
		cout << "failed to open trace file" << endl;
		system("pause");
		return 0;
	}
	//ofstream log_file;
	char rw;
	char addr[15];
	uint64_t iaddr;
	bool hit;
	Cache cache(8, 1, 2, 0);
	while (!trace_file.eof()) {
		trace_file >> rw >> addr;
		iaddr = hextoi(addr);
		if (rw == 'r') {
			hit = cache.read(iaddr);
		}
		else {
			hit = cache.write(iaddr);
		}
		//if (hit) {
		//	cout << "Hit" << endl;
		//}
		//else {
		//	cout << "Miss" << endl;
		//}
	}
	system("pause");
	return 0;
}
#else
int main(int argc, char* argv[]) {
	srand(0); // 保证实验结果的可重复性
	if (argc < 7) {
		cout << "应该有6个输入参数，依次为：" << endl;
		cout << "* 块大小\t比如8、32、64等" << endl;
		cout << "* 映射规则\t用代号表示：0-直接映射，1-全关联，2-四路组关联，3-八路组关联" << endl;
		cout << "* 替换算法\t用代号表示：0-LRU，1-随机替换，2-二叉树替换" << endl;
		cout << "* 写策略\t用代号表示：0-写不分配+写直达，1-写分配+写回，2-写不分配+写回，3-写分配+写直达" << endl;
		cout << "* 输入trace文件路径" << endl;
		cout << "* 输出log文件路径" << endl;
		return 0;
	}
	ifstream trace_file;
	ofstream log_file;
	trace_file.open(argv[5]);
	log_file.open(argv[6]);
	if (!trace_file.is_open()) {
		cout << "无法打开trace文件" << endl;
		return 0;
	}
	if (!log_file.is_open()) {
		cout << "无法打开log文件" << endl;
		return 0;
	}
	auto block_size = atoi(argv[1]);
	auto mapping_algo = atoi(argv[2]);
	auto replacing_algo = atoi(argv[3]);
	auto writing_algo = atoi(argv[4]);
	const char* str_m[] = { "直接映射","全关联","4-way组关联","8-way组关联" };
	const char* str_r[] = { "LRU","随机替换","二叉树替换" };
	const char* str_w[] = { "写不分配+写直达","写分配+写回","写不分配+写回","写分配+写直达" };
	cout << "[Cache信息]" << endl;
	cout << "总容量128KB，块大小" << block_size << "B\n";
	cout << "映射规则：" << str_m[mapping_algo] << endl;
	cout << "替换算法：" << str_r[replacing_algo] << endl;
	cout << "写策略：" << str_w[writing_algo] << endl << endl;
	char rw;
	char addr[15];
	uint64_t iaddr;
	bool hit;
	int hit_count = 0, total_count = 0;
	Cache cache(block_size, mapping_algo, replacing_algo, writing_algo);
	while (!trace_file.eof()) {
		trace_file >> rw >> addr;
		iaddr = hextoi(addr);
		++total_count;
		switch (rw) {
		case 'r':
			hit = cache.read(iaddr);
			break;
		case 'w':
			hit = cache.write(iaddr);
			break;
		default:
			cout << "Error on line " << total_count << endl;
			return 0;
		}
		if (hit) {
			log_file << "Hit" << endl;
			++hit_count;
		}
		else {
			log_file << "Miss" << endl;
		}
	}
	cout << "[模拟结果]\n";
	cout << "一共" << total_count << "次读/写操作，其中" << hit_count << "次命中，" << total_count - hit_count << "次缺失，命中率为" << double(hit_count) / total_count << "，缺失率为" << 1 - double(hit_count) / total_count << endl;
	return 0;
}
#endif