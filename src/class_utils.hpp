
namespace zit {

/**
 * Delete copy and assignment for a class
 */
struct DeleteCopyAndAssignment {
  DeleteCopyAndAssignment() = default;
  DeleteCopyAndAssignment(const DeleteCopyAndAssignment&) = delete;
  DeleteCopyAndAssignment& operator=(const DeleteCopyAndAssignment&) = delete;
};

}  // namespace zit
