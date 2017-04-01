// Slightly more advanced example that shows a bunch of
// JSON data -> object -> JSON data roundtrips
// with varying kinds of objects.

#include <string>
#include <vector>

#include <rpoco/json.hpp>

struct Ser1 {
	int x=0;

	RPOCO(x);
};

struct Ser2 {
	int a=0;
	Ser1 sub;

	RPOCO(a,sub);
};

struct Ser2P {
	int a=0;
	Ser1 *sub=0;
	RPOCO(a,sub);
};

struct SerVI {
	std::vector<int> ints;

	RPOCO(ints);
};

struct SerPVI {
	std::vector<int> *ints=0;
	
	RPOCO(ints);
};

struct SerStrs {
	std::string a;
	char b[6];

	SerStrs() {
		b[0]=0;
	}

	RPOCO(a,b);
};

template <typename T>
struct roundtrip {
	roundtrip(std::string in) {
		T test;
		if (!rpoco::parse_json(in,test)) {
			printf("Error parsing:%s\n",in.c_str());
		}
		std::string out=rpoco::to_json(test);
		printf("In:<< %s >> Out:<< %s >>\n",in.c_str(),out.c_str());
	}
};
template <typename T>
struct roundtrip<T*> {
	roundtrip(std::string in) {
		T* test=0;
		if (!rpoco::parse_json(in,test)) {
			printf("Error parsing:%s\n",in.c_str());
		}
		std::string out=rpoco::to_json(test);
		printf("In:<< %s >> Out:<< %s >>\n",in.c_str(),out.c_str());
		if (test)
			delete test;
	}
};

using rpoco::json_value;

int main(int argc,char **argv) {

	roundtrip<Ser1>("{\"x\":30}");

	roundtrip<Ser2>("{}");

	roundtrip<Ser2>("{\"sub\":{\"x\":34},\"a\":12}");

	roundtrip<Ser2P>("{}");
	roundtrip<Ser2P>("{\"sub\":{\"x\":34},\"a\":12}");

	roundtrip<SerVI>("{\"ints\":[1,23,456,78,9]}");
	roundtrip<SerPVI>("{\"ints\":null}");
	roundtrip<SerPVI>("{\"ints\":[1,23,456,78,9]}");

	roundtrip<json_value*>("null");
	roundtrip<json_value*>("123");
	roundtrip<json_value*>("567.13");
	roundtrip<json_value*>("true");
	roundtrip<json_value*>("false");
	roundtrip<json_value*>("\"Hello world\"");
	roundtrip<json_value*>("  {\"hello\":[1,2,\"world\",true,false,{  \"x\":3,\"y\":4},null,1e20]}  ");

	roundtrip<Ser1*>("null");
	roundtrip<Ser1*>("{}");
	roundtrip<Ser1*>("{\"x\":30}");
	roundtrip<std::unique_ptr<Ser1>>("null");
	roundtrip<std::unique_ptr<Ser1>>("{}");
	roundtrip<std::unique_ptr<Ser1>>("{\"x\":30}");
	roundtrip<std::shared_ptr<Ser1>>("null");
	roundtrip<std::shared_ptr<Ser1>>("{}");
	roundtrip<std::shared_ptr<Ser1>>("{\"x\":30}");

	roundtrip<SerStrs>("{}");
	roundtrip<SerStrs>("{\"a\":\"hello\", \"b\":\"world\"}");
	roundtrip<SerStrs>("{\"a\":\"hello\", \"b\":\"toobig-shouldfail\"}");

	return 0;
}
