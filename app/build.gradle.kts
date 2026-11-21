import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val patchCode = providers.gradleProperty("patchVersionCode").orElse(providers.environmentVariable("OWNCUT_VERSION_CODE")).orElse("2").get().toInt()
require(patchCode in 2..2100000000) { "Invalid patch version code" }
val signingPath = System.getenv("OWNCUT_KEYSTORE_PATH")
val hasSigning = !signingPath.isNullOrBlank()
if (providers.gradleProperty("requireStableSigning").orNull == "true") require(hasSigning) { "Stable signing is required for downloadable patches" }

android {
    buildFeatures { buildConfig = true }
    namespace = "com.owncut.app"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.owncut.app"
        minSdk = 26
        targetSdk = 36
        versionCode = patchCode
        versionName = "0.2.$patchCode"
        buildConfigField("String", "PREVIEW_REVISION", "\"" + providers.environmentVariable("GITHUB_SHA").orElse("local").get().filter { it.isLetterOrDigit() }.take(12) + "\"")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-Wall", "-Wextra")
                arguments += listOf("-DOWNCUT_BUILD_TESTS=OFF")
            }
        }
    }

    signingConfigs {
        if(hasSigning) create("patch") {
            storeFile = file(signingPath!!)
            storePassword = requireNotNull(System.getenv("OWNCUT_KEYSTORE_PASSWORD"))
            keyAlias = requireNotNull(System.getenv("OWNCUT_KEY_ALIAS"))
            keyPassword = requireNotNull(System.getenv("OWNCUT_KEY_PASSWORD"))
        }
    }
    buildTypes {
        debug { if(hasSigning) signingConfig = signingConfigs.getByName("patch") }

        release {
            if(hasSigning) signingConfig = signingConfigs.getByName("patch")
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlin {
        compilerOptions { jvmTarget = JvmTarget.JVM_17 }
    }

    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs { useLegacyPackaging = false }
    }
}

dependencies {
    implementation("androidx.media3:media3-transformer:1.8.1")
    implementation("androidx.media3:media3-effect:1.8.1")
    implementation("androidx.media3:media3-exoplayer:1.8.1")
    androidTestImplementation("androidx.test:core:1.7.0")
    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}
