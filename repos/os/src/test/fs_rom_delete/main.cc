#include <base/component.h>
#include <base/attached_rom_dataspace.h>


namespace Test_fs_rom_delete
{
	using namespace Genode;

	class Main;
};


class Test_fs_rom_delete::Main
{
	private:

		Env&                   _env;
		Attached_rom_dataspace _rom   { _env, "test.txt" };
		
		/*
		 * In case the file got deleted, the fs_rom only serves an empty ROM if
		 * the following member declaration is uncommented.
		 */
		//Signal_handler<Main> _handler { _env.ep(), *this, &Main::_update };

		void _update() { }

	public:

		 Main(Env& env) : _env { env }
		 {
			 while (true) {
				_rom.update();
				log(_rom.local_addr<const char>());
			 }
		 }
};


void Component::construct(Genode::Env& env)
{
	static Test_fs_rom_delete::Main main { env };
}
