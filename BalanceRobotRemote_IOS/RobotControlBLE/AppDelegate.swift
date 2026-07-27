//
//  AppDelegate.swift
//  RobotControlBLE
//

import UIKit

@UIApplicationMain
class AppDelegate: UIResponder, UIApplicationDelegate {

    var window: UIWindow?

    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions:
                        [UIApplication.LaunchOptionsKey: Any]?) -> Bool {

        // Programatik root — storyboard yok.
        window = UIWindow(frame: UIScreen.main.bounds)
        
        // Tüm uygulamayı koyu temaya zorla
        if #available(iOS 13.0, *) {
            window?.overrideUserInterfaceStyle = .dark
            window?.backgroundColor = .systemBackground
        } else {
            window?.backgroundColor = UIColor(red: 0.06, green: 0.06, blue: 0.08, alpha: 1.0)
        }

        let controlVC   = ControlViewController()
        let assistantVC = AssistantViewController()
        let settingsVC  = SettingsViewController()

        controlVC.title   = "Control"
        assistantVC.title = "Claude"
        settingsVC.title  = "Settings"

        // SF Symbols iOS 13+. Eski sürümlerde isim ile fallback.
        if #available(iOS 13.0, *) {
            controlVC.tabBarItem   = UITabBarItem(title: "Control",
                                                  image: UIImage(systemName: "gamecontroller"),
                                                  tag: 0)
            assistantVC.tabBarItem = UITabBarItem(title: "Claude",
                                                  image: UIImage(systemName: "waveform.circle"),
                                                  tag: 1)
            settingsVC.tabBarItem  = UITabBarItem(title: "Settings",
                                                  image: UIImage(systemName: "slider.horizontal.3"),
                                                  tag: 2)
        } else {
            controlVC.tabBarItem   = UITabBarItem(title: "Control",  image: nil, tag: 0)
            assistantVC.tabBarItem = UITabBarItem(title: "Claude",   image: nil, tag: 1)
            settingsVC.tabBarItem  = UITabBarItem(title: "Settings", image: nil, tag: 2)
        }

        let tab = UITabBarController()
        let nav1 = UINavigationController(rootViewController: controlVC)
        let nav2 = UINavigationController(rootViewController: assistantVC)
        let nav3 = UINavigationController(rootViewController: settingsVC)
        tab.viewControllers = [nav1, nav2, nav3]

        window?.rootViewController = tab
        window?.makeKeyAndVisible()
        return true
    }
}
