from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class AxidevIoRecipe(ConanFile):
    name = "axidev-io"
    version = "0.3.1"
    package_type = "library"

    # Paramètres de base
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        # The CMake target uses an underscore in the actual library target name
        # (CMake target `axidev::io` is an alias for the target `axidev_io`).
        # Ensure the packaged library name matches the built artifact.
        self.cpp_info.libs = ["axidev_io"]
        self.cpp_info.set_property("cmake_target_name", "axidev::io")
        # Make the expected CMake config file name explicit for modern Conan
        self.cpp_info.set_property("cmake_file_name", "axidev-io")
