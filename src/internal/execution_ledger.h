#pragma once

#include <rbfsafe/execution_ledger.h>

#include <filesystem>
#include <string>

namespace rbfsafe::internal {

class ExecutionLedgerWriteLock {
  public:
    ExecutionLedgerWriteLock(const ExecutionLedgerWriteLock&) = delete;
    ExecutionLedgerWriteLock& operator=(const ExecutionLedgerWriteLock&) = delete;
    ExecutionLedgerWriteLock(ExecutionLedgerWriteLock&& other) noexcept;
    ExecutionLedgerWriteLock& operator=(ExecutionLedgerWriteLock&&) = delete;
    ~ExecutionLedgerWriteLock();

    static Result<ExecutionLedgerWriteLock> acquire(const std::filesystem::path& directory);

  private:
    explicit ExecutionLedgerWriteLock(std::filesystem::path path);

    std::filesystem::path path_;
    bool held_ = true;
};

std::string execution_controller_completion_message(const ExecutionControllerCompletion& completion);
std::string execution_controller_completion_identity(const ExecutionControllerCompletion& completion);
std::string execution_ledger_identity(const std::string& session_id);
std::string execution_ledger_record_identity(const ExecutionLedgerRecord& record);
std::string execution_ledger_summary_identity(const ExecutionLedgerSummary& summary);
std::string execution_ledger_command_decision_identity(const ExecutionLedgerCommandDecision& decision);
std::string execution_ledger_audit_report_identity(const ExecutionLedgerAuditReport& report);

Result<void> append_execution_ledger_record_file(const std::filesystem::path& directory,
                                                 const ExecutionLedgerRecord& record);

} // namespace rbfsafe::internal
