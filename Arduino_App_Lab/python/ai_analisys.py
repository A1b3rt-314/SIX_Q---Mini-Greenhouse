# SPDX-FileCopyrightText: 2026 Alberto Prestes
# SPDX-License-Identifier: 0BSD

"""
SIX_Q - Manual AI image analysis

This module:
- reads a local cultivation image;
- sends it to OpenAI only when explicitly requested;
- asks GPT-6 Astra for a strict structured analysis;
- returns a Python dictionary.

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


def encode_image(
    image_path
):

    with open(
        image_path,
        "rb"
    ) as image_file:

        return (
            base64.b64encode(
                image_file.read()
            )
            .decode(
                "ascii"
            )
        )


def extract_output_text(
    response
):

    for item in response.get(
        "output",
        []
    ):

        if item.get(
            "type"
        ) != "message":

            continue


        for content in item.get(
            "content",
            []
        ):

            if content.get(
                "type"
            ) == "output_text":

                return content.get(
                    "text"
                )


    raise RuntimeError(
        "OpenAI response did not contain output_text."
    )


def analyze_image(
    image_path
):

    if not OPENAI_API_KEY:

        raise RuntimeError(
            "OPENAI_API_KEY is not configured."
        )


    encoded_image = encode_image(
        image_path
    )


    payload = {

        "model":
            MODEL,

        "reasoning": {
            "effort": "low"
        },

        "instructions":
            (
                "You are a vegetation and plant-care visual analysis "
                "system for a small experimental cultivation device. "
                "Analyze only what is visibly supported by the image. "
                "Do not invent hidden information. "
                "If no plant is visible, plant must be false, "
                "plant_number must be 0, and health and "
                "development_state must be not_applicable."
            ),

        "input": [
            {
                "role": "user",

                "content": [

                    {
                        "type": "input_text",

                        "text":
                            (
                                "Analyze this cultivation image. "
                                "Determine whether plants are visible, "
                                "count them, estimate their visible "
                                "development stage and health, and "
                                "classify the visible substrate."
                            )
                    },

                    {
                        "type": "input_image",

                        "image_url":
                            (
                                "data:image/png;base64,"
                                +
                                encoded_image
                            )
                    }
                ]
            }
        ],

        "text": {

            "format": {

                "type":
                    "json_schema",

                "name":
                    "plant_analysis",

                "strict":
                    True,

                "schema":
                    PLANT_ANALYSIS_SCHEMA
            }
        }
    }


    request = urllib.request.Request(
        OPENAI_URL,

        data=json.dumps(
            payload
        ).encode(
            "utf-8"
        ),

        headers={
            "Authorization":
                f"Bearer {OPENAI_API_KEY}",

            "Content-Type":
                "application/json"
        },

        method="POST"
    )


    try:

        with urllib.request.urlopen(
            request,
            timeout=120
        ) as response:

            response_data = json.loads(
                response.read()
                .decode(
                    "utf-8"
                )
            )


    except urllib.error.HTTPError as error:

        details = (
            error.read()
            .decode(
                "utf-8",
                errors="replace"
            )
        )


        raise RuntimeError(
            f"OpenAI HTTP {error.code}: {details}"
        ) from error


    except urllib.error.URLError as error:

        raise RuntimeError(
            f"OpenAI connection error: {error}"
        ) from error


    output_text = extract_output_text(
        response_data
    )


    analysis = json.loads(
        output_text
    )


    return {
        "analysis":
            analysis,

        "usage":
            response_data.get(
                "usage",
                {}
            ),

        "response_id":
            response_data.get(
                "id"
            )
    }