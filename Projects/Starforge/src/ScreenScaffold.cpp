// ScreenScaffold.cpp — see ScreenScaffold.h (AP-03, contract §5, E02 / F01).

#include "ScreenScaffold.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string ReadAll(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary);
            if (!in) return {};
            std::stringstream ss; ss << in.rdbuf();
            return ss.str();
        }
        bool WriteAll(const fs::path& p, const std::string& text)
        {
            std::error_code ec; fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << text;
            return (bool)out;
        }
        std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
        {
            if (from.empty()) return s;
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
            return s;
        }
        // Start of the line containing `pos`, and one past its '\n'.
        size_t LineStart(const std::string& s, size_t pos) { const size_t n = s.rfind('\n', pos); return n == std::string::npos ? 0 : n + 1; }
        size_t LineEnd(const std::string& s, size_t pos)   { const size_t n = s.find('\n', pos);  return n == std::string::npos ? s.size() : n + 1; }
    }

    std::string ScreenScaffold::DefaultStubPath()
    {
        return (fs::path("assets") / "projects" / "Starforge" / "editor" / "stubs" / "ScreenScript.h.in").generic_string();
    }

    bool ScreenScaffold::ValidName(const std::string& name)
    {
        if (name.empty()) return false;
        if (!(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
        for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
        return true;
    }

    std::string ScreenScaffold::ScenePath(const std::string& root, const std::string& name)
    { return (fs::path(root) / "scenes" / (name + ".cscene")).generic_string(); }
    std::string ScreenScaffold::ScriptPath(const std::string& root, const std::string& name)
    { return (fs::path(root) / "src" / "screens" / (name + "Screen.h")).generic_string(); }
    std::string ScreenScaffold::ModulePath(const std::string& root)
    { return (fs::path(root) / "src" / "Module.cpp").generic_string(); }

    std::string ScreenScaffold::RenderStub(const std::string& stub, const std::string& screen, const std::string& project)
    {
        return ReplaceAll(ReplaceAll(stub, "@SCREEN@", screen), "@PROJECT_NAME@", project);
    }

    bool ScreenScaffold::ModuleHasScript(const std::string& text, const std::string& screen)
    {
        return text.find("CS_SCRIPT(" + screen + "Screen)") != std::string::npos;
    }

    bool ScreenScaffold::InsertIntoModule(std::string& text, const std::string& screen, std::string* error)
    {
        const size_t b = text.find(kMarkerBegin);
        const size_t e = text.find(kMarkerEnd);
        if (b == std::string::npos || e == std::string::npos || e < b)
        {
            if (error) *error = kNoMarkersMessage;
            return false;
        }
        const std::string cls     = screen + "Screen";
        const std::string include = "#include \"screens/" + cls + ".h\"";

        // 1) CS_SCRIPT block before the END marker's line (idempotent).
        if (!ModuleHasScript(text, screen))
        {
            const size_t endLine = LineStart(text, text.find(kMarkerEnd));
            // Match the marker line's indentation.
            std::string indent;
            for (size_t i = endLine; i < text.size() && (text[i] == ' ' || text[i] == '\t'); ++i) indent += text[i];
            const std::string block = indent + "CS_SCRIPT(" + cls + ")\n" +
                                      indent + "    CS_FIELD(ExampleField)\n" +
                                      indent + "CS_END;\n";
            text.insert(endLine, block);
        }

        // 2) the include after the last #include that precedes CS_MODULE_BEGIN.
        if (text.find(include) == std::string::npos)
        {
            // The macro invocation, not a mention of it in the header comment (every
            // template's Module.cpp explains "CS_MODULE_BEGIN/END ..." in a // line, which
            // used to win here and put the include above the comment block).
            size_t moduleBegin = std::string::npos;
            for (size_t p = text.find("CS_MODULE_BEGIN"); p != std::string::npos; p = text.find("CS_MODULE_BEGIN", p + 1))
            {
                const size_t ls = LineStart(text, p);
                const size_t firstNonSpace = text.find_first_not_of(" 	", ls);
                if (firstNonSpace == p) { moduleBegin = p; break; }
            }
            const size_t limit = moduleBegin == std::string::npos ? text.size() : moduleBegin;
            size_t lastInc = std::string::npos, pos = 0;
            while ((pos = text.find("#include", pos)) != std::string::npos && pos < limit)
            {
                lastInc = pos; pos += 8;
            }
            if (lastInc != std::string::npos)
                text.insert(LineEnd(text, lastInc), include + "\n");
            else if (moduleBegin != std::string::npos)
                text.insert(LineStart(text, moduleBegin), include + "\n\n");
            else
                text.insert(0, include + "\n");
        }
        return true;
    }

    bool ScreenScaffold::WriteScreenScene(const std::string& root, const std::string& name, bool withScript, std::string* error)
    {
        if (!ValidName(name)) { if (error) *error = "'" + name + "' is not a valid screen name (identifier)"; return false; }
        const fs::path path = ScenePath(root, name);
        std::error_code ec;
        if (fs::exists(path, ec)) { if (error) *error = "scenes/" + name + ".cscene already exists"; return false; }

        Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();
        {
            Cosmic::Entity cam = scene->CreateEntity("Camera");
            auto& cc = cam.AddComponent<Cosmic::CameraComponent>();
            cc.Primary = true;
            cc.ProjectionType = Cosmic::CameraComponent::Projection::Orthographic;
            cam.GetComponent<Cosmic::TransformComponent>().Position = { 0.0f, 0.0f, 10.0f };
        }
        {
            Cosmic::Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<Cosmic::CanvasComponent>();
            if (withScript)
                canvas.AddComponent<Cosmic::NativeScriptComponent>().ClassName = name + "Screen";
        }
        fs::create_directories(path.parent_path(), ec);
        if (!Cosmic::SceneSerializer::Save(*scene, path.generic_string()))
        {
            if (error) *error = "could not write " + path.generic_string();
            return false;
        }
        return true;
    }

    bool ScreenScaffold::LinkScriptInScene(const std::string& root, const std::string& name, std::string* error)
    {
        const fs::path path = ScenePath(root, name);
        std::error_code ec;
        if (!fs::exists(path, ec)) { if (error) *error = "scenes/" + name + ".cscene does not exist"; return false; }
        Cosmic::Ref<Cosmic::Scene> scene = Cosmic::Scene::Create();
        if (!Cosmic::SceneSerializer::Load(*scene, path.generic_string()))
        { if (error) *error = "could not load " + path.generic_string(); return false; }

        Cosmic::Entity canvas;
        auto& reg = scene->GetRegistry();
        for (auto e : reg.view<Cosmic::CanvasComponent>()) { canvas = Cosmic::Entity(e, scene.get()); break; }
        if (!canvas)
        {
            for (auto e : reg.view<Cosmic::TagComponent>())
                if (reg.get<Cosmic::TagComponent>(e).Tag == "Canvas") { canvas = Cosmic::Entity(e, scene.get()); break; }
        }
        if (!canvas) { if (error) *error = "scene has no Canvas entity to carry the screen script"; return false; }
        const std::string cls = name + "Screen";
        if (canvas.HasComponent<Cosmic::NativeScriptComponent>())
        {
            auto& nsc = canvas.GetComponent<Cosmic::NativeScriptComponent>();
            if (nsc.ClassName == cls) return true;   // already linked
            nsc.ClassName = cls; nsc.Fields.clear();
        }
        else
            canvas.AddComponent<Cosmic::NativeScriptComponent>().ClassName = cls;
        if (!Cosmic::SceneSerializer::Save(*scene, path.generic_string()))
        { if (error) *error = "could not write " + path.generic_string(); return false; }
        return true;
    }

    bool ScreenScaffold::AddFlowState(Cosmic::FlowAsset& asset, const std::string& name, std::string* error)
    {
        if (!ValidName(name)) { if (error) *error = "'" + name + "' is not a valid screen name"; return false; }
        if (asset.Find(name)) { if (error) *error = "the flow already has a state named '" + name + "'"; return false; }
        Cosmic::FlowState s;
        s.Name  = name;
        s.Scene = "project://scenes/" + name + ".cscene";
        // Place the node to the right of the last one so the graph stays readable.
        float maxX = 40.0f, y = 60.0f;
        for (const auto& st : asset.States) { maxX = std::max(maxX, st.EditorPos.x + 340.0f); y = st.EditorPos.y; }
        s.EditorPos = { asset.States.empty() ? 40.0f : maxX, y };
        asset.States.push_back(s);
        if (asset.Start.empty()) asset.Start = name;
        return true;
    }

    ScreenScaffoldResult ScreenScaffold::CreateScript(const std::string& root, const std::string& name,
                                                      const std::string& projectName, const std::string& stubPath)
    {
        ScreenScaffoldResult r;
        if (!ValidName(name)) { r.Message = "'" + name + "' is not a valid screen name (identifier)"; return r; }

        // Accept the module text FIRST — a refusal must leave every file untouched.
        const fs::path modulePath = ModulePath(root);
        std::string moduleText = ReadAll(modulePath);
        if (moduleText.empty()) { r.Message = "src/Module.cpp is missing or empty"; return r; }
        std::string err;
        if (!InsertIntoModule(moduleText, name, &err)) { r.Message = err; return r; }

        const std::string stub = ReadAll(stubPath);
        if (stub.empty()) { r.Message = "screen script stub missing: " + stubPath; return r; }

        const fs::path header = ScriptPath(root, name);
        std::error_code ec;
        if (!fs::exists(header, ec))
        {
            if (!WriteAll(header, RenderStub(stub, name, projectName))) { r.Message = "could not write " + header.generic_string(); return r; }
            r.Written.push_back(header.generic_string());
        }
        if (!WriteAll(modulePath, moduleText)) { r.Message = "could not write " + modulePath.generic_string(); return r; }
        r.Written.push_back(modulePath.generic_string());
        r.Ok = true;
        r.Message = "src/screens/" + name + "Screen.h + CS_SCRIPT(" + name + "Screen) in Module.cpp";
        return r;
    }
}
