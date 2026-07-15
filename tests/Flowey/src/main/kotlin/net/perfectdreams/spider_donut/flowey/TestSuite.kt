package net.perfectdreams.spider_donut.flowey

import kotlinx.serialization.Serializable

@Serializable
data class TestSuite(
    val tests: List<TestEntry>
) {
    @Serializable
    data class TestEntry(
        val name: String,
        val commercialGame: Boolean = false,
        val spiderDonutArgs: List<String>,
        val expectedStdoutOutput: List<List<String>> = listOf(),
        val expectedStderrOutput: List<List<String>> = listOf(),
        val expectedScreenshots: List<ExpectedScreenshot> = listOf()
    ) {
        @Serializable
        data class ExpectedScreenshot(
            val expected: String,
            val actual: String
        )
    }
}
