// ParentUser — minimal mirror of `parents/{parentId}` in Firestore.
// id == Firebase Auth uid.

class ParentUser {
  final String id;
  final String name;
  final String email;
  final DateTime createdAt;

  const ParentUser({
    required this.id,
    required this.name,
    required this.email,
    required this.createdAt,
  });

  factory ParentUser.fromMap(String id, Map<String, dynamic> m) => ParentUser(
        id: id,
        name: (m['name'] ?? '') as String,
        email: (m['email'] ?? '') as String,
        createdAt: m['createdAt'] is DateTime
            ? m['createdAt'] as DateTime
            : DateTime.now(),
      );

  Map<String, dynamic> toMap() => {
        'name': name,
        'email': email,
        'createdAt': createdAt,
      };
}
