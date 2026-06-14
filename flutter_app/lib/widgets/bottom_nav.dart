// BottomNav — 5-tab material-style bottom navigation with active-pill
// highlight and offline-dot indicator on the Device tab when the device is
// offline. Translated from `BottomNav` in ~/Downloads/ioT/shared.jsx.

import 'package:flutter/material.dart';

import '../theme.dart';

enum NavTab { dashboard, reports, trends, device, settings }

class BottomNav extends StatelessWidget {
  const BottomNav({
    super.key,
    required this.active,
    required this.onChange,
    this.deviceOffline = false,
  });

  final NavTab active;
  final ValueChanged<NavTab> onChange;
  final bool deviceOffline;

  static const _items = <_NavItem>[
    _NavItem(NavTab.dashboard, Icons.home_rounded, 'בית'),
    _NavItem(NavTab.reports,   Icons.description_outlined, 'דוחות'),
    _NavItem(NavTab.trends,    Icons.trending_up_rounded, 'מגמות'),
    _NavItem(NavTab.device,    Icons.devices_other_rounded, 'התקן'),
    _NavItem(NavTab.settings,  Icons.settings_rounded, 'הגדרות'),
  ];

  @override
  Widget build(BuildContext context) {
    return Container(
      decoration: const BoxDecoration(
        color: Colors.white,
        border: Border(top: BorderSide(color: AppColors.divider, width: 1.5)),
      ),
      padding: const EdgeInsets.only(top: 8, bottom: 6, left: 4, right: 4),
      child: SafeArea(
        top: false,
        child: Row(
          children: [
            for (final it in _items)
              Expanded(
                child: InkResponse(
                  onTap: () => onChange(it.tab),
                  radius: 36,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        AnimatedContainer(
                          duration: const Duration(milliseconds: 150),
                          width: 58,
                          height: 30,
                          decoration: BoxDecoration(
                            color: it.tab == active
                                ? AppColors.skySoft
                                : Colors.transparent,
                            borderRadius:
                                BorderRadius.circular(AppRadii.pill),
                          ),
                          alignment: Alignment.center,
                          child: Stack(
                            clipBehavior: Clip.none,
                            alignment: Alignment.center,
                            children: [
                              Icon(
                                it.icon,
                                size: 23,
                                color: it.tab == active
                                    ? AppColors.sky
                                    : AppColors.inkSoft,
                              ),
                              if (it.tab == NavTab.device && deviceOffline)
                                Positioned(
                                  top: -2,
                                  right: -2,
                                  child: Container(
                                    width: 9,
                                    height: 9,
                                    decoration: BoxDecoration(
                                      color: AppColors.coral,
                                      shape: BoxShape.circle,
                                      border: Border.all(
                                          color: Colors.white, width: 2),
                                    ),
                                  ),
                                ),
                            ],
                          ),
                        ),
                        const SizedBox(height: 4),
                        Text(
                          it.label,
                          style: TextStyle(
                            fontWeight: FontWeight.w700,
                            fontSize: 11.5,
                            color: it.tab == active
                                ? AppColors.sky
                                : AppColors.inkSoft,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

class _NavItem {
  final NavTab tab;
  final IconData icon;
  final String label;
  const _NavItem(this.tab, this.icon, this.label);
}
