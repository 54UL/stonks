#include <Stnks.hpp>
#include <main.hpp>
#include <UI/UI.hpp>
#include <memory>

using namespace stnks;

int main(int argc, char* argv[])
{
    std::shared_ptr<App> app = std::make_shared<SDL2App>(COMPILED_EXEC_NAME);
    std::shared_ptr<Globals> globals = std::make_shared<Globals>();
    auto engine = std::make_shared<Engine>(app);

    RegisterDependency(Globals, globals);
    RegisterDependency(Engine, engine);

    auto ui = std::make_shared<UI>(engine);
    app->AddExecutionPipeline(ui);

    if (app->Init(argc, argv))
        return 1;

    int exitCode = app->Exec();

    Dependency::getInstance().Clear();

    return exitCode;
}
