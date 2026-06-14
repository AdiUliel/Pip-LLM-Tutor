allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

val newBuildDir: Directory =
    rootProject.layout.buildDirectory
        .dir("../../build")
        .get()
rootProject.layout.buildDirectory.value(newBuildDir)

subprojects {
    val newSubprojectBuildDir: Directory = newBuildDir.dir(project.name)
    project.layout.buildDirectory.value(newSubprojectBuildDir)
}
subprojects {
    project.evaluationDependsOn(":app")
}

// Some plugins (e.g. file_picker >=11) ship Kotlin-only Android code but
// rely on the host app to apply the Kotlin Gradle Plugin when AGP >= 9.
// Without this, the plugin's Kotlin classes never compile and `javac`
// fails on the generated registrant.
val kgpInjectedSubprojects = setOf("file_picker")
subprojects {
    if (name in kgpInjectedSubprojects) {
        plugins.withId("com.android.library") {
            if (!plugins.hasPlugin("org.jetbrains.kotlin.android")) {
                apply(plugin = "org.jetbrains.kotlin.android")
            }
        }
    }
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
