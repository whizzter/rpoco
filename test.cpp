#include <string>
#include <vector>

#include <rpoco/rpocojson.hpp>

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

template <typename T>
struct roundtrip {
	roundtrip(std::string in) {
		T test;
		if (!rpocojson::parse(in,test)) {
			printf("Error parsing:%s\n",in.c_str());
		}
		std::string out=rpocojson::to_json(&test);
		printf("In:<< %s >> Out:<< %s >>\n",in.c_str(),out.c_str());
	}
};

int main(int argc,char **argv) {

	roundtrip<Ser1>("{\"x\":30}");

	roundtrip<Ser2>("{}");

	roundtrip<Ser2>("{\"sub\":{\"x\":34},\"a\":12}");

	roundtrip<Ser2P>("{}");
	roundtrip<Ser2P>("{\"sub\":{\"x\":34},\"a\":12}");

	roundtrip<SerVI>("{\"ints\":[1,23,456,78,9]}");

	Ser1 s1;
	s1.x=1;

	std::string s1str=rpocojson::to_json(&s1);
	printf("%s\n",s1str.c_str());

	Ser2 s2;
	s2.a=2;
	s2.sub.x=1;

	std::string s2str=rpocojson::to_json(&s2);
	printf("%s\n",s2str.c_str());

	Ser2P s2a;
	Ser2P s2b;
	
	s2a.a=s2b.a=3;
	s2b.sub=&s1;
	s2b.sub->x=1;

	std::string s2astr=rpocojson::to_json(&s2a);
	std::string s2bstr=rpocojson::to_json(&s2b);
	printf("%s\n",s2astr.c_str());
	printf("%s\n",s2bstr.c_str());

	SerVI svi;
	svi.ints={1,23,456};

	std::string svistr=rpocojson::to_json(&svi);
	printf("%s\n",svistr.c_str());

	Ser1 d1;
	rpocojson::parse(s1str,d1);
	std::string d1str=rpocojson::to_json(&d1);
	printf("%s\n",d1str.c_str());


	return 0;
}
