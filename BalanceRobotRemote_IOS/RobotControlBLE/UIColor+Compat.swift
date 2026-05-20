//
//  UIColor+Compat.swift
//  RobotControlBLE
//
//  Geri uyumluluk için (iOS 13'ten önce):
//    UIColor.systemGray3 / systemGray4 / systemGray5 → yok.
//    Bu dosya iOS sürümüne bakarak ya gerçek system rengini ya da
//    benzer ton bir UIColor döndürür.
//

import UIKit

extension UIColor {
    
    // Sistemde varsa onu, yoksa düz UIColor döndüren yardımcılar.
    static var compatSystemGray3: UIColor {
        if #available(iOS 13.0, *) { return .systemGray3 }
        return UIColor(white: 0.78, alpha: 1.0)
    }
    
    static var compatSystemGray4: UIColor {
        if #available(iOS 13.0, *) { return .systemGray4 }
        return UIColor(white: 0.82, alpha: 1.0)
    }
    
    static var compatSystemGray5: UIColor {
        if #available(iOS 13.0, *) { return .systemGray5 }
        return UIColor(white: 0.90, alpha: 1.0)
    }
    
    // Bunlar iOS 7+'da var ama yine de tek bir noktadan referans verelim.
    static var compatSystemBlue:   UIColor { return UIColor(red: 0.00, green: 0.48, blue: 1.00, alpha: 1.0) }
    static var compatSystemGreen:  UIColor { return UIColor(red: 0.20, green: 0.78, blue: 0.35, alpha: 1.0) }
    static var compatSystemRed:    UIColor { return UIColor(red: 1.00, green: 0.23, blue: 0.19, alpha: 1.0) }
    static var compatSystemOrange: UIColor { return UIColor(red: 1.00, green: 0.58, blue: 0.00, alpha: 1.0) }
    static var compatSystemGray:   UIColor { return UIColor(white: 0.56, alpha: 1.0) }
    
    // groupTableViewBackground iOS 13'te deprecate ama hala var
    static var compatGroupTableViewBackground: UIColor {
        return UIColor(red: 0.10, green: 0.10, blue: 0.12, alpha: 1.0)
    }
    
    // ─── Custom dark palette (yumuşak antrasit, saf siyah değil) ───
    //
    // Önce iOS 13+ system renkleri yerine kendi paletimizi sabitliyoruz ki
    // dark mode'da çok karanlık görünüp gözü yormasın.
    
    /// Ekran arka planı — koyu antrasit
    static var compatScreenBackground: UIColor {
        return UIColor(red: 0.13, green: 0.14, blue: 0.16, alpha: 1.0)
    }
    
    /// Kart arka planı (ekrandan biraz açık)
    static var compatCardBackground: UIColor {
        return UIColor(red: 0.19, green: 0.20, blue: 0.23, alpha: 1.0)
    }
    
    /// Panel/telemetri arka planı (kart ile arasında orta ton)
    static var compatPanelBackground: UIColor {
        return UIColor(red: 0.16, green: 0.17, blue: 0.20, alpha: 1.0)
    }
    
    /// Vurgu / aksan rengi (accent için)
    static var compatAccent: UIColor {
        return UIColor(red: 0.30, green: 0.70, blue: 1.00, alpha: 1.0)
    }
    
    /// Birincil metin (kart/panel üzerinde)
    static var compatPrimaryText: UIColor {
        return UIColor(red: 0.92, green: 0.93, blue: 0.95, alpha: 1.0)
    }
    
    /// İkincil metin (alt başlık, değer etiketi)
    static var compatSecondaryText: UIColor {
        return UIColor(red: 0.62, green: 0.65, blue: 0.70, alpha: 1.0)
    }
    
    /// Ayraç çizgisi
    static var compatSeparator: UIColor {
        return UIColor(red: 0.28, green: 0.30, blue: 0.34, alpha: 1.0)
    }
}
