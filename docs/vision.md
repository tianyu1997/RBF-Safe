# Vision

RBF-Safe provides reusable, certifiable knowledge of robot configuration space.
Its purpose is to move repeated collision reasoning from every online consumer
into an inspectable geometric safety memory that can be shared by planning,
inverse kinematics, optimization, auditing, learning, and execution systems.

The core owns robot/scene identity, conservative region validation, adaptive
partitioning, region storage, and connectivity claims. Integrations remain
separate consumers. This keeps the safety claim small enough to test and audit.
