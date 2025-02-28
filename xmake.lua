add_rules("mode.debug", "mode.release")

set_languages("c++20")

add_requires("gtest", {configs = {main = true, gmock = false}})
add_requires("cpp-httplib")

target("kcache")
    set_kind("binary")
    add_packages("cpp-httplib")
    add_files("src/*.cpp")

target("test_http")
    set_kind("binary")
    add_includedirs("src")
    add_packages("cpp-httplib")
    add_files("test/test_http.cpp")
    add_files("src/*.cpp|main.cpp")

-- 定义一个函数来创建测试目标
function add_test(name, file)
    target(name)
        set_kind("binary")
        add_includedirs("src")
        add_packages("gtest")
        add_files(file)
        add_files("src/*.cpp|main.cpp")
end

-- 遍历 test 目录，自动为每个测试文件创建一个测试目标
for _, file in ipairs(os.files("test/*.cpp")) do
    local name = path.basename(file)  -- 获取文件名（不含扩展名）
    if (name ~= "test_http") then
        add_test(name, file)
    end
end
