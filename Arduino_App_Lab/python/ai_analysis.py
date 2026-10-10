# SPDX-FileCopyrightText: 2026 Alberto Prestes
# SPDX-License-Identifier: 0BSD

"""
SIX_Q - Manual AI image analysis

- Counts all visible plants.
- Classifies the visible substrate.
- Evaluates development and health only for the plant
  with the largest visible leaf area.
- Uses one image and one API request per analysis.

It has no authority over WATER, LIGHT or any actuator.
"""

import base64
import json
import urllib.error
import urllib.request

from secrets_auth import OPENAI_API_KEY


OPENAI_URL = "https://api.openai.com/v1/responses"
MODEL = "gpt-6-astra"


PLANT_ANALYSIS_SCHEMA = {
    "type": "object",
    "properties": {
        "plant": {
            "type": "boolean"
        },
        "plant_number": {
            "type": "integer",
            "minimum": 0
        },
        "health": {
            "type": "string",
            "enum": [
                "healthy",
                "damaged",
                "dead",
                "unknown",
                "not_applicable"
            ]
        },
        "development_state": {
            "type": "string",
            "enum": [
                "Seedling",
                "Vegetative",
                "Flowering",
                "Fruiting",
                "Unknown",
                "not_applicable"
            ]
        },
        "substrate": {
            "type": "string",
            "enum": [
                "Soil",
                "Clay",
                "Sand",
                "Other",
                "Unknown"
            ]
        }
    },
    "required": [
        "plant",
        "plant_number",
        "health",
        "development_state",
        "substrate"
    ],
    "additionalProperties": False
}


def encode_image(image_path):
    with open(image_path, "rb") as image_file:
        return base64.b64encode(
            image_file.read()
        ).decode("ascii")


def extract_output_text(response):
    for item in response.get("output", []):
        if item.get("type") != "message":
            continue

        for content in item.get("content", []):
            if content.get("type") == "output_text":
                return content.get("text")

    raise RuntimeError(
        "OpenAI response did not contain output_text."
    )


def analyze_image(image_path):
    if not OPENAI_API_KEY:
        raise RuntimeError(
            "OPENAI_API_KEY is not configured."
        )

    encoded_image = encode_image(image_path)

    payload = {
        "model": MODEL,

        "reasoning": {
            "effort": "high"
        },

        "instructions": (
            "You are a visual plant-analysis system for a small "
            "experimental cultivation device. "
            "Analyze only what is visibly supported by this single image. "
            "Do not invent hidden information, diagnose specific causes, "
            "or infer changes over time. "

            "COUNTING: "
            "Count all distinct visible plants, including seedlings. "
            "Use separate stems or growth centers where visible. "
            "Do not count individual leaves as separate plants. "
            "Do not invent plants hidden behind other plants. "

            "PRIMARY PLANT: "
            "Select the plant with the largest visible leaf area, "
            "including discolored or damaged leaves. "
            "The longest stem does not necessarily indicate "
            "the largest plant. "
            "If plants have similar visible leaf area, select the one "
            "whose growth center is closest to the image center. "
            "Both health and development_state must describe ONLY "
            "this same primary plant. "
            "Do not transfer signs of damage from smaller plants "
            "to the primary plant. "
            "plant_number describes ALL visible plants. "
            "substrate describes the visible growing medium. "

            "DEVELOPMENT: "
            "Classify the primary plant as Seedling, Vegetative, "
            "Flowering, Fruiting, or Unknown using visible structures. "
            "A long stem alone does not establish maturity or flowering. "

            "HEALTH: "
            "Assess the overall visible condition of the primary plant, "
            "considering its development stage and natural leaf shape. "
            "For this application, damaged requires clear, significant "
            "morphological deterioration. Color alone is insufficient. "

            "Relevant morphological signs include abnormally shriveled "
            "or contracted-looking leaves, pronounced abnormal curling "
            "with tissue deterioration, marked wilting with loss of "
            "support, collapsed tissue, dry-looking crumpled tissue, "
            "or severe deformation affecting the central growth. "
            "In seedlings, a markedly elongated weak stem accompanied "
            "by poorly developed AND visibly deteriorated leaves "
            "also supports damaged. "
            "Judge the extent and severity of these signs. "
            "A severe localized injury affecting the main stem or "
            "central growth can be significant even when other "
            "leaves remain green. "

            "Do not mistake naturally ruffled or wavy lettuce leaves, "
            "normal leaf curvature, a single inclined leaf, overlapping "
            "leaves, or perspective for morphological deterioration. "
            "An isolated small hole, notch, torn edge, or minor blemish "
            "does not by itself qualify as damaged when the remaining "
            "foliage and central growth appear intact. "

            "COLOR: "
            "Apparent color depends on illumination, exposure, "
            "white balance, and natural plant pigmentation. "
            "Light green color, uniform pallor, or brightness alone "
            "must NOT trigger damaged. "
            "Yellow, brown, or black areas support damaged only when "
            "accompanied by clear morphological or tissue deterioration. "
            "Green tissue does not override clear structural damage. "
            "Do not confuse soil, seed coats, shadows, reflections, "
            "or overexposure with damaged tissue. "
            "Do not claim a color change or leaf shrinkage over time "
            "from a single image. "

            "HEALTH CATEGORIES: "
            "Use healthy when the plant appears generally intact "
            "and has no clear significant morphological deterioration. "
            "Minor isolated imperfections are compatible with healthy. "
            "This label means no significant visible deterioration "
            "under these criteria, not proof of physiological health. "
            "Use damaged when significant morphological deterioration "
            "is clearly visible, as defined above. "
            "Use dead only when the visible plant as a whole appears "
            "dead, with no evident viable growth. "
            "One brown or dry leaf is not enough to classify it as dead. "
            "Use unknown when blur, occlusion, image corruption, "
            "poor lighting, or overexposure prevents a reliable "
            "assessment of the plant's structure and tissue. "

            "If no plant is visible, set plant to false, plant_number "
            "to 0, and health and development_state to not_applicable. "
            "If plants are visible, set plant to true and plant_number "
            "to at least 1. "
            "Return only the JSON required by the schema."
        ),

        "input": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_text",
                        "text": (
                            "Count all visible plants and classify "
                            "the visible substrate. "
                            "Assess development and health ONLY for "
                            "the plant with the largest visible leaf area. "
                            "Base damaged on significant visible "
                            "morphological deterioration, not color alone."
                        )
                    },
                    {
                        "type": "input_image",
                        "image_url": (
                            "data:image/png;base64,"
                            + encoded_image
                        )
                    }
                ]
            }
        ],

        "text": {
            "format": {
                "type": "json_schema",
                "name": "plant_analysis",
                "strict": True,
                "schema": PLANT_ANALYSIS_SCHEMA
            }
        }
    }

    request = urllib.request.Request(
        OPENAI_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {OPENAI_API_KEY}",
            "Content-Type": "application/json"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(
            request,
            timeout=120
        ) as response:
            response_data = json.loads(
                response.read().decode("utf-8")
            )

    except urllib.error.HTTPError as error:
        details = error.read().decode(
            "utf-8",
            errors="replace"
        )
        raise RuntimeError(
            f"OpenAI HTTP {error.code}: {details}"
        ) from error

    except urllib.error.URLError as error:
        raise RuntimeError(
            f"OpenAI connection error: {error}"
        ) from error

    output_text = extract_output_text(response_data)
    analysis = json.loads(output_text)

    return {
        "analysis": analysis,
        "usage": response_data.get("usage", {}),
        "response_id": response_data.get("id")
    }