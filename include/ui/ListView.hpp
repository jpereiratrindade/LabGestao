#pragma once
#include "domain/ProjectStore.hpp"
#include "domain/ProjectScaffold.hpp"

namespace labgestao {

class ListView {
public:
    struct CreationDefaults {
        std::string cppRoot;
        std::string pythonRoot;
    };

    explicit ListView(ProjectStore& store, CreationDefaults defaults = {});
    void render();
    void requestCreateProject();

private:
    void resetCreateForm();
    void renderTable();
    void renderDetailPanel();
    void renderOpenInEditorSection(const Project& p);
    void renderAdrSection(Project& p);
    void renderDaiSection(Project& p);
    void renderMetricsSection(const Project& p);
    void renderCreateModal();
    void renderEditModal();
    void renderDeleteConfirm();

    ProjectStore& m_store;

    // State
    char  m_searchBuf[256]{};
    int   m_filterStatus{-1}; // -1 = all
    std::string m_selectedId;
    bool  m_showCreate{false};
    bool  m_showEdit{false};
    bool  m_showDeleteConfirm{false};

    // Form buffers
    char  m_formName[256]{};
    char  m_formDesc[1024]{};
    char  m_formCategory[128]{};
    char  m_formTags[512]{};
    int   m_formStatus{0};
    std::string m_formError;
    int   m_createTemplate{static_cast<int>(ProjectTemplate::Cpp)};
    bool  m_createOnDisk{true};
    char  m_scaffoldBaseDir[512]{};
    CreationDefaults m_creationDefaults;

    // ADR form
    char  m_adrTitle[256]{};
    char  m_adrContext[512]{};
    char  m_adrDecision[512]{};
    char  m_adrConsequences[512]{};

    // DAI form
    int   m_daiKind{1}; // 0=Decision, 1=Action, 2=Impediment
    char  m_daiTitle[256]{};
    char  m_daiOwner[128]{};
    char  m_daiDueAt[32]{};
    char  m_daiNotes[512]{};

    // Editor integration
    int   m_editorChoice{0}; // 0=codium, 1=code, 2=system, 3=custom
    char  m_customEditorCmd[256]{};
    std::string m_editorMessage;
};

} // namespace labgestao
