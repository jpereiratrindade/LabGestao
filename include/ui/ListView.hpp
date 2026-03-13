#pragma once
#include "domain/ProjectStore.hpp"

namespace labgestao {

class ListView {
public:
    explicit ListView(ProjectStore& store);
    void render();

private:
    void renderTable();
    void renderDetailPanel();
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
};

} // namespace labgestao
