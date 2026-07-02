// MaterialDetailScreen — shows the contents of one uploaded material: a preview
// of the attached file (images inline, other types open externally) plus the
// Q/A pairs the device draws questions from.

import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:url_launcher/url_launcher.dart';

import '../constants.dart';
import '../models/material_doc.dart';
import '../theme.dart';
import '../widgets/p_card.dart';
import '../widgets/screen_header.dart';

class MaterialDetailScreen extends StatelessWidget {
  const MaterialDetailScreen({super.key, required this.material});

  final MaterialDoc material;

  bool get _hasFile =>
      material.fileUrl != null && material.fileUrl!.isNotEmpty;

  /// Firebase Storage URLs carry query params, so test the path before '?'.
  bool get _isImage {
    if (!_hasFile) return false;
    final path = material.fileUrl!.toLowerCase().split('?').first;
    return path.endsWith('.png') ||
        path.endsWith('.jpg') ||
        path.endsWith('.jpeg');
  }

  @override
  Widget build(BuildContext context) {
    final meta = subjectMeta[material.subject]!;
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            ScreenHeader(
              title: material.title,
              subtitle:
                  '${meta.heLabel} · ${DateFormat('d MMM yyyy', 'he').format(material.uploadedAt)}',
              onBack: () => Navigator.of(context).maybePop(),
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.fromLTRB(20, 4, 20, 20),
                children: [
                  if (_hasFile)
                    _FileSection(url: material.fileUrl!, isImage: _isImage),
                  if (material.items.isNotEmpty) ...[
                    const SizedBox(height: 18),
                    Padding(
                      padding: const EdgeInsets.only(right: 2, bottom: 8),
                      child: Text(
                        'שאלות (${material.items.length})',
                        style: AppTextStyles.title(context)
                            .copyWith(fontSize: 16),
                      ),
                    ),
                    for (var i = 0; i < material.items.length; i++) ...[
                      _QACard(index: i + 1, qa: material.items[i]),
                      const SizedBox(height: 10),
                    ],
                  ],
                  if (!_hasFile && material.items.isEmpty)
                    Padding(
                      padding: const EdgeInsets.only(top: 40),
                      child: Text(
                        material.isExtractionPending
                            ? 'מעבד את הקובץ… השאלות יופיעו כאן בקרוב.'
                            : 'אין תוכן להצגה.',
                        textAlign: TextAlign.center,
                        style: AppTextStyles.hint(context),
                      ),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _FileSection extends StatelessWidget {
  const _FileSection({required this.url, required this.isImage});

  final String url;
  final bool isImage;

  Future<void> _open(BuildContext context) async {
    final messenger = ScaffoldMessenger.of(context);
    final ok = await launchUrl(
      Uri.parse(url),
      mode: LaunchMode.externalApplication,
    );
    if (!ok) {
      messenger.showSnackBar(
        const SnackBar(content: Text('לא ניתן לפתוח את הקובץ')),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return PCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (isImage)
            ClipRRect(
              borderRadius: BorderRadius.circular(AppRadii.sm),
              child: Image.network(
                url,
                fit: BoxFit.contain,
                loadingBuilder: (c, child, progress) => progress == null
                    ? child
                    : const Padding(
                        padding: EdgeInsets.all(40),
                        child: Center(child: CircularProgressIndicator()),
                      ),
                errorBuilder: (c, e, s) => const Padding(
                  padding: EdgeInsets.all(24),
                  child: Center(child: Text('לא ניתן לטעון את התמונה')),
                ),
              ),
            )
          else
            Row(
              children: [
                Container(
                  width: 48,
                  height: 48,
                  decoration: BoxDecoration(
                    color: AppColors.skySoft,
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: const Icon(Icons.insert_drive_file_outlined,
                      color: AppColors.sky, size: 24),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    'קובץ מצורף',
                    style:
                        AppTextStyles.title(context).copyWith(fontSize: 15.5),
                  ),
                ),
              ],
            ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: () => _open(context),
            icon: const Icon(Icons.open_in_new, color: AppColors.sky),
            label: Text(isImage ? 'פתיחה במסך מלא' : 'פתיחת הקובץ'),
          ),
        ],
      ),
    );
  }
}

class _QACard extends StatelessWidget {
  const _QACard({required this.index, required this.qa});

  final int index;
  final QAPair qa;

  @override
  Widget build(BuildContext context) {
    return PCard(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'שאלה $index',
            style: TextStyle(
              fontWeight: FontWeight.w800,
              color: AppColors.inkSoft,
              fontSize: 13,
            ),
          ),
          const SizedBox(height: 6),
          Text(
            qa.question,
            style: AppTextStyles.title(context).copyWith(fontSize: 15),
          ),
          const SizedBox(height: 6),
          Row(
            children: [
              const Icon(Icons.check_circle_outline,
                  color: Color(0xFF1E9C7E), size: 18),
              const SizedBox(width: 6),
              Expanded(
                child: Text(
                  qa.answer,
                  style: AppTextStyles.hint(context).copyWith(
                    fontSize: 14,
                    color: const Color(0xFF1E9C7E),
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}
