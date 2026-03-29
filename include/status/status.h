#ifndef STATUS_STATUS_H_
#define STATUS_STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Project-wide status codes shared across firmware layers.
/// @note Zero means success. Negative values describe project-defined failure
///     reasons without exposing backend-specific errno values.
typedef enum status_code {
	/// @brief The operation completed successfully.
	STATUS_OK = 0,
	/// @brief A caller supplied an invalid argument.
	STATUS_ERR_INVALID_ARGUMENT = -1,
	/// @brief The requested operation cannot proceed yet.
	STATUS_ERR_NOT_READY = -2,
	/// @brief A required device or resource is unavailable.
	STATUS_ERR_DEVICE_UNAVAILABLE = -3,
	/// @brief A lower-level backend operation failed unexpectedly.
	STATUS_ERR_BACKEND = -4,
} status_code_t;

#ifdef __cplusplus
}
#endif

#endif /* STATUS_STATUS_H_ */