// MaterialUploadScreen — 3 sub-tabs: existing materials list, manual Q/A
// entry, file upload (PDF / image / txt). Translates the design from
// ~/Downloads/ioT/screen-config.jsx.

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../constants.dart';
import '../models/child.dart';
import '../models/material_doc.dart';
import '../providers/child_provider.dart';
import '../services/firebase_service.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/screen_header.dart';

enum _MaterialTab { list, manual, file }

class MaterialUploadScreen extends StatefulWidget {
  const MaterialUploadScreen({super.key});

  @override
  State<MaterialUploadScreen> createState() => _MaterialUploadScreenState();
}

class _MaterialUploadScreenState extends State<MaterialUploadScreen> {
  _MaterialTab _tab = _MaterialTab.list;
  Subject _subject = Subject.math;
  final _titleCtrl = TextEditingController();
  final List<_QARow> _pairs = [_QARow()];
  PlatformFile? _file;
  String? _fileError;
  bool _saving = false;

  @override
  void dispose() {
    _titleCtrl.dispose();
    for (final p in _pairs) {
      p.dispose();
    }
    super.dispose();
  }

  Child? get _child => context.read<ChildProvider>().child;

  bool get _manualValid =>
      _titleCtrl.text.trim().isNotEmpty &&
      _pairs.any((p) => p.q.text.trim().isNotEmpty && p.a.text.trim().isNotEmpty);

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: _build(),
      ),
    );
  }

  Widget _build() {
    switch (_tab) {
      case _MaterialTab.list:
        return _list();
      case _MaterialTab.manual:
        return _manual();
      case _MaterialTab.file:
        return _filePicker();
    }
  }

  // ─────────────────────────── LIST tab ───────────────────────────

  Widget _list() {
    final child = _child;
    final fb = context.read<FirebaseService>();
    return Column(
      children: [
        ScreenHeader(
          title: 'חומרי לימוד',
          subtitle: 'שאלות שההתקן ישתמש בהן',
          onBack: () => Navigator.of(context).maybePop(),
          right: _AddButton(onTap: () {
            setState(() => _tab = _MaterialTab.manual);
          }),
        ),
        Expanded(
          child: child == null
              ? const Center(child: CircularProgressIndicator())
              : StreamBuilder<List<MaterialDoc>>(
                  stream: fb.watchMaterials(child.id),
                  builder: (context, snap) {
                    if (snap.hasError) {
                      return ListView(
                        padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
                        children: [
                          _ErrorBanner(
                            text:
                                'בעיה בטעינת רשימת הקבצים: ${snap.error}',
                          ),
                          const SizedBox(height: 12),
                          _UploadDashed(
                            onTap: () =>
                                setState(() => _tab = _MaterialTab.file),
                          ),
                        ],
                      );
                    }
                    final items = snap.data ?? const <MaterialDoc>[];
                    return ListView(
                      padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
                      children: [
                        for (final m in items) ...[
                          _MaterialTile(
                            material: m,
                            onDelete: () => _confirmDelete(m),
                            onEnabledChanged: (v) => fb.setMaterialEnabled(
                              m.id,
                              v,
                            ),
                          ),
                          const SizedBox(height: 12),
                        ],
                        const SizedBox(height: 4),
                        _UploadDashed(
                          onTap: () =>
                              setState(() => _tab = _MaterialTab.file),
                        ),
                      ],
                    );
                  },
                ),
        ),
      ],
    );
  }

  // ─────────────────────────── MANUAL tab ───────────────────────────

  Widget _manual() {
    return Column(
      children: [
        ScreenHeader(
          title: 'שאלות חדשות',
          subtitle: 'הקלדה ידנית',
          onBack: () => setState(() => _tab = _MaterialTab.list),
        ),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                PCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('מקצוע', style: AppTextStyles.label(context)),
                      const SizedBox(height: 8),
                      _SubjectChooser(
                        value: _subject,
                        onChange: (s) => setState(() => _subject = s),
                      ),
                      const SizedBox(height: 16),
                      Text('כותרת', style: AppTextStyles.label(context)),
                      const SizedBox(height: 8),
                      TextField(
                        controller: _titleCtrl,
                        decoration: const InputDecoration(
                          hintText: 'לדוגמה: לוח הכפל',
                        ),
                        onChanged: (_) => setState(() {}),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 20),
                Padding(
                  padding: const EdgeInsets.only(right: 2, bottom: 10),
                  child: Text(
                    'שאלות ותשובות',
                    style: AppTextStyles.title(context).copyWith(fontSize: 16),
                  ),
                ),
                for (var i = 0; i < _pairs.length; i++) ...[
                  _QAEditor(
                    index: i,
                    row: _pairs[i],
                    canRemove: _pairs.length > 1,
                    onChange: () => setState(() {}),
                    onRemove: () {
                      setState(() {
                        _pairs.removeAt(i).dispose();
                      });
                    },
                  ),
                  const SizedBox(height: 12),
                ],
                OutlinedButton.icon(
                  onPressed: () => setState(() => _pairs.add(_QARow())),
                  icon: const Icon(Icons.add, color: AppColors.sky),
                  label: const Text('עוד שאלה'),
                ),
              ],
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 8, 20, 22),
          child: ElevatedButton(
            onPressed:
                (!_manualValid || _saving) ? null : _saveManual,
            child: Text(_saving ? 'שומר…' : 'שמירה'),
          ),
        ),
      ],
    );
  }

  Future<void> _confirmDelete(MaterialDoc m) async {
    final messenger = ScaffoldMessenger.of(context);
    final fb = context.read<FirebaseService>();
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('מחיקת קובץ'),
        content: Text('למחוק את "${m.title}"? פעולה זו אינה הפיכה.'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(false),
            child: const Text('ביטול'),
          ),
          TextButton(
            style: TextButton.styleFrom(foregroundColor: AppColors.coral),
            onPressed: () => Navigator.of(ctx).pop(true),
            child: const Text('מחיקה'),
          ),
        ],
      ),
    );
    if (ok != true) return;
    try {
      await fb.deleteMaterial(m);
    } catch (e) {
      messenger.showSnackBar(
        SnackBar(content: Text('המחיקה נכשלה: $e')),
      );
    }
  }

  Future<void> _saveManual() async {
    final child = _child;
    if (child == null) return;
    setState(() => _saving = true);
    final items = [
      for (final p in _pairs)
        if (p.q.text.trim().isNotEmpty && p.a.text.trim().isNotEmpty)
          QAPair(question: p.q.text.trim(), answer: p.a.text.trim()),
    ];
    final doc = MaterialDoc(
      id: '',
      childId: child.id,
      subject: _subject,
      title: _titleCtrl.text.trim(),
      items: items,
      uploadedAt: DateTime.now(),
    );
    await context.read<FirebaseService>().uploadMaterial(doc);
    if (!mounted) return;
    setState(() {
      _saving = false;
      _titleCtrl.clear();
      for (final p in _pairs) {
        p.dispose();
      }
      _pairs
        ..clear()
        ..add(_QARow());
      _tab = _MaterialTab.list;
    });
  }

  // ─────────────────────────── FILE tab ───────────────────────────

  Widget _filePicker() {
    return Column(
      children: [
        ScreenHeader(
          title: 'העלאת קובץ',
          subtitle: 'שיעורי בית',
          onBack: () => setState(() => _tab = _MaterialTab.list),
        ),
        Expanded(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(20, 4, 20, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                PCard(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('מקצוע', style: AppTextStyles.label(context)),
                      const SizedBox(height: 8),
                      _SubjectChooser(
                        value: _subject,
                        onChange: (s) => setState(() => _subject = s),
                      ),
                    ],
                  ),
                ),
                const SizedBox(height: 14),
                InkWell(
                  onTap: _pickFile,
                  borderRadius: BorderRadius.circular(AppRadii.md),
                  child: DottedBorderContainer(
                    selected: _file != null,
                    child: Padding(
                      padding: const EdgeInsets.symmetric(
                          vertical: 34, horizontal: 16),
                      child: Column(
                        children: [
                          Icon(
                            _file == null
                                ? Icons.upload_outlined
                                : Icons.description_outlined,
                            size: 36,
                            color: AppColors.sky,
                          ),
                          const SizedBox(height: 10),
                          Text(
                            _file?.name ?? 'בחירת קובץ מהמכשיר',
                            style: AppTextStyles.title(context)
                                .copyWith(fontSize: 16),
                            textAlign: TextAlign.center,
                          ),
                          const SizedBox(height: 3),
                          Text(
                            _file == null
                                ? 'PDF · JPG · PNG · עד 5MB'
                                : '${(_file!.size / 1024).round()} ק״ב · לחצו לבחירה אחרת',
                            style: AppTextStyles.hint(context)
                                .copyWith(fontSize: 12.5),
                            textAlign: TextAlign.center,
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
                if (_fileError != null) ...[
                  const SizedBox(height: 12),
                  _ErrorBanner(text: _fileError!),
                ],
                if (_file != null && _fileError == null) ...[
                  const SizedBox(height: 14),
                  _OkBanner(text: 'הקובץ עבר בדיקת תקינות'),
                ],
              ],
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(20, 8, 20, 22),
          child: ElevatedButton(
            onPressed: (_file == null || _saving) ? null : _saveFile,
            child: Text(_saving ? 'מעלה…' : 'העלאה ושמירה'),
          ),
        ),
      ],
    );
  }

  Future<void> _pickFile() async {
    final res = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: AppConstants.allowedFileExtensions,
      withData: true,
    );
    if (res == null || res.files.isEmpty) return;
    final f = res.files.first;
    if (f.size > AppConstants.maxUploadBytes) {
      setState(() {
        _file = null;
        _fileError =
            'הקובץ גדול מדי (${(f.size / 1024 / 1024).toStringAsFixed(1)} מ״ב). מקסימום 5 מ״ב.';
      });
      return;
    }
    setState(() {
      _file = f;
      _fileError = null;
    });
  }

  Future<void> _saveFile() async {
    final child = _child;
    final f = _file;
    if (child == null || f == null) return;
    setState(() => _saving = true);
    final doc = MaterialDoc(
      id: '',
      childId: child.id,
      subject: _subject,
      title: f.name,
      items: const [],
      uploadedAt: DateTime.now(),
    );
    await context.read<FirebaseService>().uploadMaterial(
          doc,
          fileBytes: f.bytes,
          fileName: f.name,
        );
    if (!mounted) return;
    setState(() {
      _saving = false;
      _file = null;
      _tab = _MaterialTab.list;
    });
  }
}

// ─────────────────────────── widgets used above ───────────────────────────

class _QARow {
  final TextEditingController q = TextEditingController();
  final TextEditingController a = TextEditingController();
  void dispose() {
    q.dispose();
    a.dispose();
  }
}

class _AddButton extends StatelessWidget {
  const _AddButton({required this.onTap});
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.pill),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 9),
        decoration: BoxDecoration(
          color: AppColors.sky,
          borderRadius: BorderRadius.circular(AppRadii.pill),
          boxShadow: AppShadow.button,
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: const [
            Icon(Icons.add, color: Colors.white, size: 18),
            SizedBox(width: 6),
            Text(
              'הוספה',
              style: TextStyle(
                color: Colors.white,
                fontWeight: FontWeight.w800,
                fontSize: 14.5,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _MaterialTile extends StatelessWidget {
  const _MaterialTile({
    required this.material,
    this.onDelete,
    this.onEnabledChanged,
  });
  final MaterialDoc material;
  final VoidCallback? onDelete;
  final ValueChanged<bool>? onEnabledChanged;

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[material.subject]!;
    final count = material.items.length;
    final when = _heAgo(material.uploadedAt);
    final disabled = !material.enabled;
    // While the Cloud Function is still parsing the uploaded file, show a
    // friendlier line than "0 שאלות".
    final subtitle = material.isExtractionPending
        ? 'מעבד שאלות… · $when'
        : '$count שאלות · $when';
    return Opacity(
      opacity: disabled ? 0.55 : 1,
      child: PCard(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              children: [
                Container(
                  width: 48,
                  height: 48,
                  decoration: BoxDecoration(
                    color: meta.tint,
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: Icon(Icons.description_outlined,
                      color: meta.ink, size: 24),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(material.title,
                          style: AppTextStyles.title(context)
                              .copyWith(fontSize: 15.5),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis),
                      const SizedBox(height: 2),
                      Text(
                        subtitle,
                        style: AppTextStyles.hint(context)
                            .copyWith(fontSize: 12.5),
                      ),
                    ],
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 11, vertical: 5),
                  decoration: BoxDecoration(
                    color: meta.tint,
                    borderRadius: BorderRadius.circular(AppRadii.pill),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(meta.emoji, style: const TextStyle(fontSize: 14)),
                      const SizedBox(width: 4),
                      Text(
                        meta.heLabel,
                        style: TextStyle(
                          fontWeight: FontWeight.w700,
                          fontSize: 12.5,
                          color: meta.ink,
                        ),
                      ),
                    ],
                  ),
                ),
                if (onDelete != null) ...[
                  const SizedBox(width: 4),
                  IconButton(
                    onPressed: onDelete,
                    icon: const Icon(Icons.delete_outline_rounded,
                        color: AppColors.coral, size: 22),
                    tooltip: 'מחיקה',
                  ),
                ],
              ],
            ),
            if (onEnabledChanged != null) ...[
              const SizedBox(height: 4),
              Row(
                children: [
                  Expanded(
                    child: Text(
                      material.enabled
                          ? 'כלול בתרגול של הילד'
                          : 'מושבת — לא ייכלל בתרגול',
                      style: AppTextStyles.hint(context)
                          .copyWith(fontSize: 12.5),
                    ),
                  ),
                  Switch(
                    value: material.enabled,
                    onChanged: onEnabledChanged,
                    activeThumbColor: Colors.white,
                    activeTrackColor: AppColors.sky,
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }

  static String _heAgo(DateTime d) {
    final days = DateTime.now().difference(d).inDays;
    if (days <= 0) return 'היום';
    if (days == 1) return 'אתמול';
    if (days < 7) return 'לפני $days ימים';
    if (days < 14) return 'לפני שבוע';
    if (days < 30) return 'לפני ${(days / 7).round()} שבועות';
    return DateFormat('d MMM', 'he').format(d);
  }
}

class _UploadDashed extends StatelessWidget {
  const _UploadDashed({required this.onTap});
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(AppRadii.md),
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 26, horizontal: 16),
        decoration: BoxDecoration(
          color: Colors.white,
          borderRadius: BorderRadius.circular(AppRadii.md),
          border: Border.all(
              color: AppColors.skySoft, width: 2.5, style: BorderStyle.solid),
        ),
        child: Column(
          children: [
            const Icon(Icons.upload_outlined, color: AppColors.sky, size: 30),
            const SizedBox(height: 8),
            Text('העלאת קובץ שיעורי בית',
                style: AppTextStyles.title(context).copyWith(fontSize: 15)),
            const SizedBox(height: 2),
            Text('PDF · תמונה · עד 5MB',
                style: AppTextStyles.hint(context).copyWith(fontSize: 12.5)),
          ],
        ),
      ),
    );
  }
}

class _SubjectChooser extends StatelessWidget {
  const _SubjectChooser({required this.value, required this.onChange});
  final Subject value;
  final ValueChanged<Subject> onChange;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        for (final s in Subject.values) ...[
          Expanded(
            child: InkWell(
              onTap: () => onChange(s),
              borderRadius: BorderRadius.circular(AppRadii.sm),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 140),
                padding: const EdgeInsets.symmetric(vertical: 12),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: value == s ? AppColors.skySoft : Colors.white,
                  borderRadius: BorderRadius.circular(AppRadii.sm),
                  border: Border.all(
                    color: value == s ? AppColors.sky : AppColors.skySoft,
                    width: value == s ? 2.5 : 2,
                  ),
                ),
                child: Text(
                  '${subjectMeta[s]!.emoji} ${subjectMeta[s]!.heLabel}',
                  style: TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 14.5,
                    color: value == s ? AppColors.sky : AppColors.inkSoft,
                  ),
                ),
              ),
            ),
          ),
          if (s != Subject.values.last) const SizedBox(width: 10),
        ],
      ],
    );
  }
}

class _QAEditor extends StatelessWidget {
  const _QAEditor({
    required this.index,
    required this.row,
    required this.canRemove,
    required this.onChange,
    required this.onRemove,
  });
  final int index;
  final _QARow row;
  final bool canRemove;
  final VoidCallback onChange;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    return PCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'שאלה ${index + 1}',
                style: TextStyle(
                  fontWeight: FontWeight.w800,
                  color: AppColors.inkSoft,
                  fontSize: 13,
                ),
              ),
              if (canRemove)
                TextButton(
                  onPressed: onRemove,
                  style: TextButton.styleFrom(
                    foregroundColor: AppColors.coral,
                    padding: EdgeInsets.zero,
                    minimumSize: const Size(40, 24),
                  ),
                  child: const Text('הסרה',
                      style: TextStyle(fontWeight: FontWeight.w700)),
                ),
            ],
          ),
          const SizedBox(height: 8),
          TextField(
            controller: row.q,
            onChanged: (_) => onChange(),
            decoration: const InputDecoration(hintText: 'השאלה'),
          ),
          const SizedBox(height: 8),
          TextField(
            controller: row.a,
            onChanged: (_) => onChange(),
            decoration: const InputDecoration(hintText: 'התשובה הנכונה'),
          ),
        ],
      ),
    );
  }
}

class DottedBorderContainer extends StatelessWidget {
  const DottedBorderContainer({
    super.key,
    required this.selected,
    required this.child,
  });
  final bool selected;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: BoxDecoration(
        color: selected ? AppColors.skySoft : Colors.white,
        borderRadius: BorderRadius.circular(AppRadii.md),
        border: Border.all(
          color: AppColors.sky,
          width: 2.5,
          // Dart's BoxDecoration doesn't support dashed; visual approximation
          // is a thicker solid border. Future polish item.
        ),
      ),
      child: child,
    );
  }
}

class _ErrorBanner extends StatelessWidget {
  const _ErrorBanner({required this.text});
  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
      decoration: BoxDecoration(
        color: AppColors.coralSoft,
        borderRadius: BorderRadius.circular(AppRadii.sm),
      ),
      child: Row(
        children: [
          const Icon(Icons.warning_amber_rounded,
              color: Color(0xFFC2425A), size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(
                color: Color(0xFFC2425A),
                fontWeight: FontWeight.w700,
                fontSize: 13.5,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _OkBanner extends StatelessWidget {
  const _OkBanner({required this.text});
  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: AppColors.mintSoft,
        borderRadius: BorderRadius.circular(AppRadii.sm),
      ),
      child: Row(
        children: [
          const Icon(Icons.check_circle_outline,
              color: Color(0xFF1E9C7E), size: 18),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(
                color: Color(0xFF1E9C7E),
                fontWeight: FontWeight.w700,
                fontSize: 13.5,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
